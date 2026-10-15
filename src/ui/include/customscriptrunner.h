#ifndef CUSTOMSCRIPTRUNNER_H
#define CUSTOMSCRIPTRUNNER_H

#include <memory>

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

// Runs a user script from the configured scripts library folder on the
// currently selected log text. The selection is written to a temporary file
// whose path is passed to the script as its first argument. The script's
// standard output is shown in a message box; scripts that open their own
// GUI simply produce no output.
// Lifecycle: the runner owns its QProcess (parent-child) and deletes itself
// once the process finishes.
class CustomScriptRunner : public QObject {
    Q_OBJECT

  public:
    // Lists supported scripts (non-recursive) found in the given folder.
    static QStringList enumerateScripts( const QString& folderPath );

    CustomScriptRunner( const QString& scriptPath, const QString& selectedText,
                        QObject* parent = nullptr );
    ~CustomScriptRunner() override;

    void start();

  private:
    void handleFinished( int exitCode, QProcess::ExitStatus exitStatus );

    QString scriptPath_;
    std::unique_ptr<QTemporaryFile> inputFile_;
    QProcess* process_ = nullptr;
};

#endif // CUSTOMSCRIPTRUNNER_H
