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

#ifndef KLOGG_NAMEDCOLORPOPUP_H
#define KLOGG_NAMEDCOLORPOPUP_H

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QVector>

class QEvent;
class QGridLayout;
class QLabel;
class QLineEdit;
class QToolButton;

// Palette of the colors Qt knows by name (the X11/SVG set, 148 entries in Qt 5.12),
// grouped per hue family and searchable. Used to pick log line highlight colors.
class NamedColorPopup : public QDialog {
    Q_OBJECT

  public:
    enum class Side
    {
        Foreground,
        Background
    };

    // Returns an invalid color if the user cancelled, like QColorDialog::getColor does.
    static QColor getColor( Side side, const QColor& foreColor, const QColor& backColor,
                            const QString& caption, QWidget* parent );

    NamedColorPopup( Side side, const QColor& foreColor, const QColor& backColor,
                     QWidget* parent = nullptr );

    QColor selectedColor() const;

  protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

  private Q_SLOTS:
    void applyFilter();
    void chooseCustomColor();

  private:
    enum Group
    {
        WhiteGroup,
        GrayGroup,
        RedPinkGroup,
        OrangeBrownGroup,
        YellowGroup,
        GreenGroup,
        CyanGroup,
        BlueGroup,
        PurpleGroup,
        GroupCount
    };

    struct Entry
    {
        QColor color;
        QString name;
        QString hex;
        QToolButton* button = nullptr;
    };

    static Group groupOf( const QColor& color );
    static QString titleOf( Group group );

    void buildPalette();
    void updatePreview( const QColor& hovered );
    int firstVisibleEntry() const;
    void selectEntry( int entryIndex );

    QVector<Entry> entries_;
    QVector<QVector<int>> groupMembers_;
    QVector<QWidget*> groupWidgets_;
    QWidget* paletteContent_ = nullptr;
    QHash<QToolButton*, int> entryIndexOf_;

    Side side_;
    QColor foreColor_;
    QColor backColor_;
    QColor selectedColor_;

    QLineEdit* searchEdit_ = nullptr;
    QLabel* previewLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
};

#endif
