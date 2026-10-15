#include "customscriptrunner.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Supported script extensions mapped to their interpreters
const QHash<QString, QString>& scriptInterpreters()
{
    static const QHash<QString, QString> interpreters = {
        { "py", "python3" },
        { "sh", "/bin/bash" },
    };
    return interpreters;
}

} // namespace

QStringList CustomScriptRunner::enumerateScripts( const QString& folderPath )
{
    QStringList scripts;
    if ( folderPath.isEmpty() ) {
        return scripts;
    }

    QDir folder( folderPath );
    if ( !folder.exists() ) {
        return scripts;
    }

    QStringList filters;
    const auto& interpreters = scriptInterpreters();
    for ( auto it = interpreters.keyValueBegin(); it != interpreters.keyValueEnd(); ++it ) {
        filters << ( "*." + ( *it ).first );
    }

    const auto entries = folder.entryInfoList( filters, QDir::Files, QDir::Name );
    for ( const auto& entry : entries ) {
        scripts << entry.absoluteFilePath();
    }
    return scripts;
}

CustomScriptRunner::CustomScriptRunner( const QString& scriptPath, const QString& selectedText,
                                        QObject* parent )
    : QObject( parent )
    , scriptPath_( scriptPath )
{
    inputFile_ = std::make_unique<QTemporaryFile>(
        QDir::temp().filePath( "klogg_custom_script_XXXXXX.txt" ) );
    if ( inputFile_->open() ) {
        inputFile_->write( selectedText.toUtf8() );
        inputFile_->flush();
    }
}

CustomScriptRunner::~CustomScriptRunner()
{
    if ( process_ != nullptr && process_->state() != QProcess::NotRunning ) {
        process_->kill();
        process_->waitForFinished( 500 );
    }
}

void CustomScriptRunner::start()
{
    const QFileInfo scriptInfo( scriptPath_ );
    const auto interpreter = scriptInterpreters().value( scriptInfo.suffix().toLower() );
    if ( interpreter.isEmpty() ) {
        QMessageBox::warning( QApplication::activeWindow(), tr( "Custom script" ),
                              tr( "Unsupported script type: %1" ).arg( scriptPath_ ) );
        deleteLater();
        return;
    }

    if ( inputFile_ == nullptr || !inputFile_->isOpen() ) {
        QMessageBox::warning( QApplication::activeWindow(), tr( "Custom script" ),
                              tr( "Failed to prepare the selection text file." ) );
        deleteLater();
        return;
    }

    process_ = new QProcess( this );
    connect( process_,
             static_cast<void ( QProcess::* )( int, QProcess::ExitStatus )>( &QProcess::finished ),
             this, [ this ]( int exitCode, QProcess::ExitStatus exitStatus ) {
                 handleFinished( exitCode, exitStatus );
             } );

    process_->start( interpreter, QStringList{ scriptPath_, inputFile_->fileName() } );
}

void CustomScriptRunner::handleFinished( int exitCode, QProcess::ExitStatus exitStatus )
{
    const auto standardOutput = QString::fromUtf8( process_->readAllStandardOutput() );
    const auto standardError = QString::fromUtf8( process_->readAllStandardError() );
    const auto scriptName = QFileInfo( scriptPath_ ).fileName();

    if ( exitStatus != QProcess::NormalExit || exitCode != 0 ) {
        QMessageBox::warning(
            QApplication::activeWindow(), tr( "Custom script" ),
            tr( "Script \"%1\" failed (exit code %2).\n%3" )
                .arg( scriptName )
                .arg( exitCode )
                .arg( standardError.isEmpty() ? standardOutput : standardError ) );
    }
    else if ( !standardOutput.trimmed().isEmpty() ) {
        // Scripts with their own GUI usually print nothing; stay silent then.
        // Scrollable dialog: script output can be long (e.g. timing reports).
        QDialog resultDialog( QApplication::activeWindow() );
        resultDialog.setWindowTitle( scriptName );
        resultDialog.resize( 720, 520 );

        auto* layout = new QVBoxLayout( &resultDialog );
        auto* textView = new QPlainTextEdit( &resultDialog );
        textView->setReadOnly( true );
        textView->setLineWrapMode( QPlainTextEdit::NoWrap );
        QFont monoFont( QStringLiteral( "Monospace" ) );
        monoFont.setStyleHint( QFont::TypeWriter );
        textView->setFont( monoFont );
        textView->setPlainText( standardOutput );
        layout->addWidget( textView );

        auto* closeButton = new QPushButton( tr( "Close" ), &resultDialog );
        connect( closeButton, &QPushButton::clicked, &resultDialog, &QDialog::accept );
        layout->addWidget( closeButton );

        resultDialog.exec();
    }

    deleteLater();
}
