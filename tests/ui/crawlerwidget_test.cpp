/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch.hpp>

#include <QSignalSpy>
#include <QFontInfo>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <qglobal.h>
#include <qnamespace.h>
#include <qtestmouse.h>

#include "savedsearches.h"
#include "session.h"
#include "test_utils.h"

#include "logdata.h"
#include "logfiltereddata.h"

#include "crawlerwidget.h"

static const qint64 SL_NB_LINES = 100LL;

namespace {
bool generateDataFiles( QTemporaryFile& file )
{
    char newLine[ 90 ];

    if ( file.open() ) {
        for ( int i = 0; i < SL_NB_LINES; i++ ) {
            snprintf( newLine, 89,
                      "LOGDATA \t is a part of glogg, we are going to test it thoroughly, this is "
                      "line %06d",
                      i );
            file.write( newLine, static_cast<qint64>( qstrlen( newLine ) ) );
#ifdef Q_OS_WIN
            file.write( "\r\n", 2 );
#else
            file.write( "\n", 1 );
#endif
        }
        file.flush();
    }

    return true;
}

} // namespace

struct CrawlerWidgetPrivate {
};

template <>
struct CrawlerWidget::access_by<CrawlerWidgetPrivate> {
    std::unique_ptr<CrawlerWidget> crawler;

    bool isLoadingFinished()
    {
        return !crawler->loadingInProgress_;
    }

    LinesCount getLogNbLines()
    {
        return crawler->logData_->getNbLine();
    }

    LinesCount getLogFilteredNbLines()
    {
        return crawler->logFilteredData_->getNbLine();
    }

    void selectAllInMainView()
    {
        crawler->logMainView_->selectAll();
    }

    void selectAllInFilteredView()
    {
        crawler->filteredView_->selectAll();
    }

    QString mainViewSelectedText()
    {
        return crawler->logMainView_->getSelectedText();
    }

    QString filteredViewSelectedText()
    {
        return crawler->filteredView_->getSelectedText();
    }

    void setSearchPattern( const QString& pattern )
    {
        QTest::keyClicks( crawler->searchLineEdit_, pattern );
    }

    void enableCaseSensitiveSearch()
    {
        if ( !crawler->matchCaseButton_->isChecked() ) {
            QTest::mouseClick( crawler->matchCaseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableInverseMatch()
    {
        if ( !crawler->inverseButton_->isChecked() ) {
            QTest::mouseClick( crawler->inverseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableBooleanCombinationMode()
    {
        if ( !crawler->booleanButton_->isChecked() ) {
            QTest::mouseClick( crawler->booleanButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void runSearch()
    {
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );

        QTest::qWait( 100 );

        waitUiState( [ & ]() { return crawler->stopButton_->isHidden(); } );
    }

    void render()
    {
        crawler->grab();
    }

    void configureFilterRules( bool enabled )
    {
        crawler->filterRules_.clear();
        CrawlerWidget::UnifiedFilterRule rule;
        rule.pattern = "LOGDATA";
        rule.enabled = enabled;
        rule.priority = 10;
        crawler->filterRules_.append( rule );
        rule.pattern = "line";
        rule.priority = 20;
        crawler->filterRules_.append( rule );
        crawler->refreshFilterRulesPanel();
        crawler->applyFilterRules();
        return;
    }

    QTableWidget* rulesTable()
    {
        return crawler->highlighterRulesTable_;
    }

    QWidget* filterPanel()
    {
        return crawler->filterControls_;
    }

    void moveFirstRuleDown()
    {
        crawler->highlighterRulesTable_->selectRow( 0 );
        crawler->moveSelectedFilterRuleDown();
        return;
    }

    QCheckBox* automaticReloadCheckBox()
    {
        return crawler->automaticReloadCheckBox_;
    }

    AbstractLogView* displayedLogView()
    {
        if ( crawler->primaryLogViewStack_->currentWidget() == crawler->logMainView_ ) {
            return crawler->logMainView_;
        }
        return crawler->filteredView_;
    }

    bool isSearchRunning()
    {
        return crawler->stopButton_->isEnabled();
    }

    void markFirstLine()
    {
        crawler->logFilteredData_->addMark( 0_lnum );
        return;
    }

    bool isFirstLineMarked()
    {
        return crawler->logFilteredData_->getMarks().contains( 0_lnum );
    }
};

using CrawlerWidgetVisitor = CrawlerWidget::access_by<CrawlerWidgetPrivate>;

SCENARIO( "Filter controls and periodic reload", "[ui][filters]" )
{
    QTemporaryFile file{ "crawler_controls_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );
    Session session;
    CrawlerWidgetVisitor visitor;
    visitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return visitor.isLoadingFinished(); } ) );
    visitor.crawler->resize( 1000, 500 );
    visitor.crawler->show();
    visitor.configureFilterRules( true );
    REQUIRE( waitUiState( [ & ]() { return !visitor.isSearchRunning(); } ) );

    SECTION( "Numbering follows display order but priorities move with rules" )
    {
        auto* table = visitor.rulesTable();
        REQUIRE( table->horizontalHeaderItem( 0 )->text() == "No." );
        REQUIRE( table->item( 0, 0 )->text() == "1" );
        REQUIRE_FALSE( table->item( 0, 0 )->flags().testFlag( Qt::ItemIsEditable ) );
        visitor.moveFirstRuleDown();
        REQUIRE( table->item( 0, 0 )->text() == "1" );
        REQUIRE( table->item( 1, 0 )->text() == "2" );
        auto* firstPriority = qobject_cast<QSpinBox*>( table->cellWidget( 0, 4 ) );
        auto* secondPriority = qobject_cast<QSpinBox*>( table->cellWidget( 1, 4 ) );
        REQUIRE( firstPriority != nullptr );
        REQUIRE( secondPriority != nullptr );
        REQUIRE( firstPriority->value() == 20 );
        REQUIRE( secondPriority->value() == 10 );
    }

    SECTION( "Ctrl J hides and restores the entire panel without clearing rules" )
    {
        visitor.crawler->registerShortcuts();
        visitor.crawler->activateWindow();
        visitor.displayedLogView()->setFocus();
        QTest::qWait( 50 );
        QTest::keyClick( visitor.displayedLogView(), Qt::Key_J, Qt::ControlModifier );
        REQUIRE( visitor.filterPanel()->isHidden() );
        REQUIRE( visitor.getLogFilteredNbLines().get() == SL_NB_LINES );
        QTest::keyClick( visitor.displayedLogView(), Qt::Key_J, Qt::ControlModifier );
        REQUIRE_FALSE( visitor.filterPanel()->isHidden() );
        REQUIRE( visitor.rulesTable()->rowCount() == 2 );
    }

    SECTION( "Panel font zoom does not change the log font" )
    {
        visitor.crawler->registerShortcuts();
        visitor.crawler->activateWindow();
        visitor.displayedLogView()->setFocus();
        QTest::qWait( 50 );
        const auto originalPanelSize = visitor.filterPanel()->fontInfo().pointSize();
        const auto originalLogFont = visitor.displayedLogView()->font();
        const auto shiftedPlus = GENERATE( false, true );
        const auto plusKey = shiftedPlus ? Qt::Key_Plus : Qt::Key_Equal;
        auto modifiers = Qt::KeyboardModifiers{ Qt::ControlModifier };
        if ( shiftedPlus ) {
            modifiers |= Qt::ShiftModifier;
        }
        QTest::keyClick( visitor.displayedLogView(), plusKey, modifiers );
        REQUIRE( visitor.filterPanel()->fontInfo().pointSize() == originalPanelSize + 1 );
        REQUIRE( visitor.rulesTable()->fontInfo().pointSize() == originalPanelSize + 1 );
        REQUIRE( visitor.displayedLogView()->font() == originalLogFont );
        QTest::keyClick( visitor.displayedLogView(), Qt::Key_Minus, Qt::ControlModifier );
        REQUIRE( visitor.filterPanel()->fontInfo().pointSize() == originalPanelSize );
    }

    SECTION( "Periodic reload restores filtering and scrolls to the bottom" )
    {
        const auto filtersEnabled = GENERATE( true, false );
        visitor.configureFilterRules( filtersEnabled );
        REQUIRE( waitUiState( [ & ]() { return !visitor.isSearchRunning(); } ) );
        auto* view = visitor.displayedLogView();
        visitor.markFirstLine();
        view->verticalScrollBar()->setValue( 0 );
        SafeQSignalSpy reloaded{ visitor.crawler.get(), SIGNAL( loadingFinished( LoadingStatus ) ) };
        visitor.automaticReloadCheckBox()->setChecked( true );
        REQUIRE( reloaded.safeWait() );
        REQUIRE( waitUiState( [ & ]() { return !visitor.isSearchRunning(); } ) );
        REQUIRE( visitor.getLogNbLines().get() == SL_NB_LINES );
        REQUIRE( visitor.isFirstLineMarked() );
        if ( filtersEnabled ) {
            REQUIRE( visitor.getLogFilteredNbLines().get() == SL_NB_LINES );
        }
        REQUIRE( view->verticalScrollBar()->value() == view->verticalScrollBar()->maximum() );
        visitor.automaticReloadCheckBox()->setChecked( false );
        const auto reloadCount = reloaded.count();
        QTest::qWait( 1200 );
        REQUIRE( reloaded.count() == reloadCount );
    }
}

SCENARIO( "Crawler widget search", "[ui]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    session.savedSearches().clear();

    REQUIRE( session.savedSearches().recentSearches().empty() );

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );

    crawlerVisitor.render();

    REQUIRE( crawlerVisitor.getLogNbLines().get() == SL_NB_LINES );

    GIVEN( "loaded log data" )
    {
        THEN( "Has no lines in log view" )
        {
            REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
        }

        WHEN( "search for lines" )
        {
            crawlerVisitor.setSearchPattern( "this is line" );
            crawlerVisitor.runSearch();

            REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                return crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES;
            } ) );

            THEN( "all lines are matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }

            AND_WHEN( "copy all from main view" )
            {
                crawlerVisitor.selectAllInMainView();
                auto text = crawlerVisitor.mainViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }

            AND_WHEN( "copy all from filtered view" )
            {
                crawlerVisitor.selectAllInFilteredView();
                auto text = crawlerVisitor.filteredViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }
        }

        WHEN( "search for 10" )
        {
            crawlerVisitor.setSearchPattern( "10" );

            crawlerVisitor.runSearch();

            waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 1; } );

            THEN( "single line match" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 1 );
            }
        }

        WHEN( "case sensitive search" )
        {
            crawlerVisitor.setSearchPattern( "THIS" );
            crawlerVisitor.enableCaseSensitiveSearch();
            crawlerVisitor.runSearch();

            THEN( "no lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
            }
        }

        WHEN( "inverse match search" )
        {
            crawlerVisitor.setSearchPattern( "not match" );
            crawlerVisitor.enableInverseMatch();
            crawlerVisitor.runSearch();

            THEN( "all lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }
        }

        WHEN( "boolean search" )
        {
            crawlerVisitor.setSearchPattern( "\"glogg\" or \"klogg\"" );
            crawlerVisitor.enableBooleanCombinationMode();
            crawlerVisitor.runSearch();

            THEN( "has lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() >= 2 );
            }
        }
    }
}
