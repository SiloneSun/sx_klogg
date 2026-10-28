#include <catch2/catch.hpp>

#include <QSettings>
#include <QTemporaryDir>

#include "highlighterset.h"

TEST_CASE( "Highlight priority overrides rule order", "[ui][highlighter]" )
{
    Highlighter first{ "error", false, false, Qt::black, Qt::red };
    Highlighter second{ "error", false, false, Qt::white, Qt::blue };
    first.setPriority( 1 );
    second.setPriority( 10 );

    auto set = HighlighterSet::createNewSet( "priority" );
    set.setHighlighters( { first, second } );
    HighlightedMatchRanges matches;
    HighlightColor lineColor;
    REQUIRE( set.matchLine( "error message", matches, &lineColor )
             == HighlighterMatchType::LineMatch );
    REQUIRE( lineColor.backColor == QColor( Qt::blue ) );
    REQUIRE( matches.front().backColor() == QColor( Qt::blue ) );

    set.setHighlighters( { second, first } );
    matches.clear();
    set.matchLine( "error message", matches, &lineColor );
    REQUIRE( lineColor.backColor == QColor( Qt::blue ) );
}

TEST_CASE( "Earlier rules win equal highlight priorities", "[ui][highlighter]" )
{
    Highlighter first{ "error", false, true, Qt::black, Qt::red };
    Highlighter second{ "error", false, true, Qt::white, Qt::blue };
    auto set = HighlighterSet::createNewSet( "sequence" );
    set.setHighlighters( { first, second } );

    HighlightedMatchRanges matches;
    REQUIRE( set.matchLine( "error", matches ) == HighlighterMatchType::WordMatch );
    REQUIRE( matches.front().backColor() == QColor( Qt::red ) );

    set.setHighlighters( { second, first } );
    matches.clear();
    set.matchLine( "error", matches );
    REQUIRE( matches.front().backColor() == QColor( Qt::blue ) );
}

TEST_CASE( "Keyword priority only overrides its matching range", "[ui][highlighter]" )
{
    Highlighter wholeLine{ "error", false, false, Qt::black, Qt::red };
    Highlighter keyword{ "error", false, true, Qt::white, Qt::blue };
    keyword.setPriority( 10 );
    auto set = HighlighterSet::createNewSet( "mixed modes" );
    set.setHighlighters( { wholeLine, keyword } );

    HighlightedMatchRanges matches;
    HighlightColor lineColor;
    set.matchLine( "error message", matches, &lineColor );
    REQUIRE( lineColor.backColor == QColor( Qt::red ) );
    REQUIRE( matches.matches().size() == 2 );
    REQUIRE( matches.front().size() == LineLength{ 5 } );
    REQUIRE( matches.front().backColor() == QColor( Qt::blue ) );
    REQUIRE( matches.back().backColor() == QColor( Qt::red ) );

    wholeLine.setPriority( 20 );
    set.setHighlighters( { wholeLine, keyword } );
    matches.clear();
    set.matchLine( "error message", matches, &lineColor );
    REQUIRE( matches.matches().size() == 1 );
    REQUIRE( matches.front().size() == LineLength{ 13 } );
    REQUIRE( lineColor.backColor == QColor( Qt::red ) );
}

TEST_CASE( "Highlight priorities survive settings round trips", "[ui][highlighter]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QSettings settings{ directory.filePath( "highlighters.ini" ), QSettings::IniFormat };
    Highlighter original{ "error", false, true, Qt::black, Qt::red };
    original.setPriority( -7 );
    original.saveToStorage( settings );

    Highlighter restored;
    restored.retrieveFromStorage( settings );
    REQUIRE( restored.priority() == -7 );

    settings.remove( "priority" );
    restored.retrieveFromStorage( settings );
    REQUIRE( restored.priority() == 0 );
}

TEST_CASE( "Reloading a compiled highlighter set rebuilds priority order", "[ui][highlighter]" )
{
    Highlighter first{ "error", false, true, Qt::black, Qt::red };
    Highlighter second{ "error", false, true, Qt::white, Qt::blue };
    first.setPriority( 10 );
    auto set = HighlighterSet::createNewSet( "reload priorities" );
    set.setHighlighters( { first, second } );
    HighlightedMatchRanges matches;
    set.matchLine( "error", matches );
    REQUIRE( matches.front().backColor() == QColor( Qt::red ) );

    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QSettings settings{ directory.filePath( "highlighters.ini" ), QSettings::IniFormat };
    second.setPriority( 20 );
    auto replacement = HighlighterSet::createNewSet( "replacement" );
    replacement.setHighlighters( { first, second } );
    replacement.saveToStorage( settings );
    set.retrieveFromStorage( settings );
    matches.clear();
    set.matchLine( "error", matches );
    REQUIRE( matches.front().backColor() == QColor( Qt::blue ) );

    replacement.setHighlighters( { first } );
    replacement.saveToStorage( settings );
    set.retrieveFromStorage( settings );
    matches.clear();
    set.matchLine( "error", matches );
    REQUIRE( matches.front().backColor() == QColor( Qt::red ) );
}
