/*
 * Copyright (C) 2026 sunxilong and other contributors
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

#include "namedcolorpopup.h"

#include <QColorDialog>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kColumns = 12;
constexpr int kButtonWidth = 24;
constexpr int kButtonHeight = 22;
constexpr int kPaletteHeight = 380;

// HSL thresholds placing a name in one of the 9 groups, tuned against the full
// QColor::colorNames() list so that no group ends up a grab bag.
constexpr double kWhiteLightness = 0.93;
constexpr double kGraySaturation = 0.12;

constexpr int kHueRedStart = 325;
constexpr int kHueOrangeStart = 15;
constexpr int kHueYellowStart = 45;
constexpr int kHueGreenStart = 75;
constexpr int kHueCyanStart = 170;
constexpr int kHueBlueStart = 200;
constexpr int kHuePurpleStart = 255;

const char kPreviewSample[] = "10-15 12:03:41.224 MI_VIF_EnableDev dev=0 ret=0";

double relativeLuminance( const QColor& color )
{
    const auto toLinear = []( double channel ) {
        return channel <= 0.03928 ? channel / 12.92
                                  : std::pow( ( channel + 0.055 ) / 1.055, 2.4 );
    };

    return 0.2126 * toLinear( color.redF() ) + 0.7152 * toLinear( color.greenF() )
           + 0.0722 * toLinear( color.blueF() );
}

// WCAG 2.x contrast ratio, 1.0 (identical) to 21.0 (black on white).
double contrastRatio( const QColor& foreground, const QColor& background )
{
    const auto fore = relativeLuminance( foreground );
    const auto back = relativeLuminance( background );
    const auto lighter = std::max( fore, back );
    const auto darker = std::min( fore, back );

    return ( lighter + 0.05 ) / ( darker + 0.05 );
}

} // namespace

NamedColorPopup::NamedColorPopup( Side side, const QColor& foreColor, const QColor& backColor,
                                  QWidget* parent )
    : QDialog( parent )
    , side_( side )
    , foreColor_( foreColor )
    , backColor_( backColor )
{
    groupMembers_.resize( GroupCount );
    groupWidgets_.resize( GroupCount );

    auto* mainLayout = new QVBoxLayout( this );

    previewLabel_ = new QLabel( QString::fromLatin1( kPreviewSample ), this );
    previewLabel_->setTextInteractionFlags( Qt::NoTextInteraction );
    mainLayout->addWidget( previewLabel_ );

    infoLabel_ = new QLabel( this );
    mainLayout->addWidget( infoLabel_ );

    searchEdit_ = new QLineEdit( this );
    searchEdit_->setPlaceholderText( tr( "Search color name or #hex" ) );
    searchEdit_->setClearButtonEnabled( true );
    mainLayout->addWidget( searchEdit_ );

    auto* scrollArea = new QScrollArea( this );
    scrollArea->setWidgetResizable( true );
    scrollArea->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    scrollArea->setFixedHeight( kPaletteHeight );
    mainLayout->addWidget( scrollArea, 1 );

    buildPalette();
    scrollArea->setWidget( paletteContent_ );

    auto* buttonRow = new QHBoxLayout();
    auto* customButton = new QPushButton( tr( "Custom..." ), this );
    auto* cancelButton = new QPushButton( tr( "Cancel" ), this );
    buttonRow->addWidget( customButton );
    buttonRow->addStretch();
    buttonRow->addWidget( cancelButton );
    mainLayout->addLayout( buttonRow );

    connect( searchEdit_, &QLineEdit::textChanged, this, &NamedColorPopup::applyFilter );
    connect( searchEdit_, &QLineEdit::returnPressed, this, [this] {
        const auto entryIndex = firstVisibleEntry();
        if ( entryIndex >= 0 ) {
            selectEntry( entryIndex );
        }
    } );
    connect( customButton, &QPushButton::clicked, this, &NamedColorPopup::chooseCustomColor );
    connect( cancelButton, &QPushButton::clicked, this, &QDialog::reject );

    updatePreview( side == Side::Foreground ? foreColor : backColor );

    resize( 420, 560 );
}

void NamedColorPopup::buildPalette()
{
    const auto names = QColor::colorNames();
    entries_.reserve( names.size() );

    for ( const auto& name : names ) {
        const QColor color( name );
        if ( !color.isValid() ) {
            continue;
        }

        Entry entry;
        entry.color = color;
        entry.name = name.toLower();
        entry.hex = color.name();

        groupMembers_[static_cast<int>( groupOf( color ) )]
            .append( static_cast<int>( entries_.size() ) );
        entries_.append( entry );
    }

    paletteContent_ = new QWidget( this );
    auto* contentLayout = new QVBoxLayout( paletteContent_ );
    contentLayout->setContentsMargins( 6, 6, 6, 6 );
    contentLayout->setSpacing( 6 );

    const auto lightnessLess = [this]( int left, int right ) {
        const auto leftHsl = entries_[left].color.toHsl();
        const auto rightHsl = entries_[right].color.toHsl();
        if ( leftHsl.lightness() != rightHsl.lightness() ) {
            return leftHsl.lightness() > rightHsl.lightness();
        }
        return leftHsl.hue() < rightHsl.hue();
    };

    for ( int group = 0; group != GroupCount; ++group ) {
        auto& members = groupMembers_[group];

        auto* groupWidget = new QWidget( paletteContent_ );
        groupWidgets_[group] = groupWidget;
        contentLayout->addWidget( groupWidget );
        if ( members.isEmpty() ) {
            groupWidget->setVisible( false );
            continue;
        }
        std::sort( members.begin(), members.end(), lightnessLess );

        auto* groupLayout = new QVBoxLayout( groupWidget );
        groupLayout->setContentsMargins( 0, 0, 0, 0 );
        groupLayout->setSpacing( 2 );

        auto* header = new QLabel( titleOf( static_cast<Group>( group ) ), groupWidget );
        groupLayout->addWidget( header );

        auto* grid = new QGridLayout();
        grid->setContentsMargins( 0, 0, 0, 0 );
        grid->setSpacing( 2 );

        for ( int position = 0; position != members.size(); ++position ) {
            const auto entryIndex = members.at( position );
            auto& entry = entries_[entryIndex];

            auto* button = new QToolButton( groupWidget );
            button->setCheckable( true );
            button->setFixedSize( kButtonWidth, kButtonHeight );
            button->setToolTip( QStringLiteral( "%1 %2" ).arg( entry.name, entry.hex ) );
            button->setStyleSheet( QStringLiteral(
                "QToolButton { background-color: %1; border: 1px solid palette(mid); }"
                "QToolButton:checked { border: 2px solid palette(dark); }" )
                .arg( entry.hex ) );
            button->installEventFilter( this );
            entryIndexOf_.insert( button, entryIndex );

            connect( button, &QToolButton::clicked, this, [this, entryIndex] {
                selectEntry( entryIndex );
            } );

            grid->addWidget( button, position / kColumns, position % kColumns );
            entry.button = button;
        }

        groupLayout->addLayout( grid );
    }

    contentLayout->addStretch();

    // Mark the color the rule already has, so the current value is findable.
    const auto current = side_ == Side::Foreground ? foreColor_ : backColor_;
    for ( const auto& entry : entries_ ) {
        if ( entry.color == current ) {
            entry.button->setChecked( true );
            break;
        }
    }

    return;
}

NamedColorPopup::Group NamedColorPopup::groupOf( const QColor& color )
{
    const auto hsl = color.toHsl();
    const auto saturation = hsl.saturation() / 255.0;
    const auto lightness = hsl.lightness() / 255.0;

    if ( lightness >= kWhiteLightness ) {
        return WhiteGroup;
    }
    if ( saturation < kGraySaturation ) {
        return GrayGroup;
    }

    const auto hue = hsl.hue();
    if ( hue >= kHueRedStart || hue < kHueOrangeStart ) {
        return RedPinkGroup;
    }
    if ( hue < kHueYellowStart ) {
        return OrangeBrownGroup;
    }
    if ( hue < kHueGreenStart ) {
        return YellowGroup;
    }
    if ( hue < kHueCyanStart ) {
        return GreenGroup;
    }
    if ( hue < kHueBlueStart ) {
        return CyanGroup;
    }
    if ( hue < kHuePurpleStart ) {
        return BlueGroup;
    }

    return PurpleGroup;
}

QString NamedColorPopup::titleOf( Group group )
{
    switch ( group ) {
    case WhiteGroup:
        return tr( "White" );
    case GrayGroup:
        return tr( "Gray & Black" );
    case RedPinkGroup:
        return tr( "Red & Pink" );
    case OrangeBrownGroup:
        return tr( "Orange & Brown" );
    case YellowGroup:
        return tr( "Yellow" );
    case GreenGroup:
        return tr( "Green" );
    case CyanGroup:
        return tr( "Cyan" );
    case BlueGroup:
        return tr( "Blue" );
    case PurpleGroup:
        return tr( "Purple" );
    case GroupCount:
        break;
    }

    return QString();
}

void NamedColorPopup::applyFilter()
{
    const auto term
        = searchEdit_->text().trimmed().toLower().remove( QLatin1Char( ' ' ) );

    for ( const auto& entry : entries_ ) {
        const auto visible = term.isEmpty() || entry.name.contains( term )
                             || entry.hex.contains( term );
        entry.button->setVisible( visible );
    }

    for ( int group = 0; group != GroupCount; ++group ) {
        auto hasVisible = std::any_of( groupMembers_[group].cbegin(),
                                       groupMembers_[group].cend(),
                                       [this]( int entryIndex ) {
                                           return entries_[entryIndex].button->isVisible();
                                       } );
        groupWidgets_[group]->setVisible( hasVisible );
    }

    return;
}

int NamedColorPopup::firstVisibleEntry() const
{
    for ( int group = 0; group != GroupCount; ++group ) {
        for ( const auto entryIndex : groupMembers_[group] ) {
            if ( entries_[entryIndex].button->isVisible() ) {
                return entryIndex;
            }
        }
    }

    return -1;
}

void NamedColorPopup::updatePreview( const QColor& hovered )
{
    auto fore = foreColor_;
    auto back = backColor_;
    if ( side_ == Side::Foreground ) {
        fore = hovered;
    }
    else {
        back = hovered;
    }

    previewLabel_->setStyleSheet( QStringLiteral(
        "QLabel { background-color: %1; color: %2; padding: 4px; border: 1px solid palette(mid); }" )
                                      .arg( back.name( QColor::HexRgb ) )
                                      .arg( fore.name( QColor::HexRgb ) ) );

    // Alpha is treated as opaque here; named colors never carry one.
    const auto ratio = contrastRatio( fore, back );
    const auto rating = ratio >= 4.5 ? tr( "good" ) : ( ratio >= 3.0 ? tr( "fair" )
                                                                     : tr( "hard to read" ) );
    infoLabel_->setText( tr( "%1 · contrast %2:1 (%3)" )
                             .arg( hovered.name( QColor::HexArgb ) )
                             .arg( ratio, 0, 'f', 1 )
                             .arg( rating ) );

    return;
}

void NamedColorPopup::selectEntry( int entryIndex )
{
    selectedColor_ = entries_.at( entryIndex ).color;
    accept();
    return;
}

void NamedColorPopup::chooseCustomColor()
{
    const auto current = side_ == Side::Foreground ? foreColor_ : backColor_;
    const auto color = QColorDialog::getColor( current, this, tr( "Select color" ),
                                               QColorDialog::ShowAlphaChannel );
    if ( !color.isValid() ) {
        return;
    }

    selectedColor_ = color;
    accept();
    return;
}

bool NamedColorPopup::eventFilter( QObject* watched, QEvent* event )
{
    auto* button = qobject_cast<QToolButton*>( watched );
    if ( button ) {
        const auto entryIndex = entryIndexOf_.constFind( button );
        if ( entryIndex != entryIndexOf_.constEnd() ) {
            if ( event->type() == QEvent::Enter ) {
                updatePreview( entries_.at( entryIndex.value() ).color );
            }
            else if ( event->type() == QEvent::Leave ) {
                updatePreview( side_ == Side::Foreground ? foreColor_ : backColor_ );
            }
        }
    }

    return QDialog::eventFilter( watched, event );
}

QColor NamedColorPopup::selectedColor() const
{
    return selectedColor_;
}

QColor NamedColorPopup::getColor( Side side, const QColor& foreColor, const QColor& backColor,
                                  const QString& caption, QWidget* parent )
{
    NamedColorPopup popup( side, foreColor, backColor, parent );
    popup.setWindowTitle( caption );

    if ( popup.exec() == QDialog::Accepted ) {
        return popup.selectedColor();
    }

    return QColor();
}
