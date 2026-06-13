// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// guiutil_hidpi_additions.cpp
//
// Paste the declarations into src/qt/guiutil.h (inside namespace GUIUtil)
// and the implementations into src/qt/guiutil.cpp.
// The HiDPI attribute block goes into src/qt/bitcoin.cpp before QApplication.
//
// ─── ADD TO src/qt/guiutil.h (inside namespace GUIUtil) ─────────────────────
//
//   /**
//    * Returns a font point size scaled to the current screen's logical DPI.
//    * Use this instead of hardcoded pixel sizes so text is readable on 4K
//    * and HiDPI displays without being huge on 1080p.
//    *
//    * @param basePoints  Desired size at 96 DPI (Qt's standard logical DPI).
//    */
//   int scaledFontPoints(int basePoints);
//
//   /**
//    * Apply a consistent DPI-aware font to the whole application.
//    * Call once from main() immediately after QApplication is constructed.
//    */
//   void applyDefaultFont(QApplication *app);
//
// ─── ADD TO src/qt/guiutil.cpp ───────────────────────────────────────────────

#include <QScreen>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QApplication>
#include <QFont>

// Place these implementations inside the GUIUtil namespace in guiutil.cpp:

// namespace GUIUtil {

int GUIUtil::scaledFontPoints(int basePoints)
{
    // Qt's logicalDotsPerInch() already incorporates the OS display-scaling
    // factor (e.g. 150% on Windows, Retina on macOS), so we don't need to
    // query devicePixelRatio separately.
    constexpr qreal kRefDpi = 96.0;

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return basePoints;

    const qreal dpi   = screen->logicalDotsPerInch();
    // Clamp: never shrink below base; cap at 4x (32K future-proofing)
    const qreal scale = qBound(1.0, dpi / kRefDpi, 4.0);
    return qMax(basePoints, qRound(basePoints * scale));
}

void GUIUtil::applyDefaultFont(QApplication *app)
{
    // Try to load bundled Inter (clean, readable at all sizes).
    // Falls back to the system sans-serif if the font resource isn't present.
    int id = QFontDatabase::addApplicationFont(
        QStringLiteral(":/fonts/Inter-Regular.ttf"));

    QString family;
    if (id >= 0 && !QFontDatabase::applicationFontFamilies(id).isEmpty())
        family = QFontDatabase::applicationFontFamilies(id).at(0);
    else
        family = app->font().family();

    QFont f(family);
    // 10pt @ 96 DPI ≈ 13 CSS px — comfortable for 1080p; scales up for 4K.
    f.setPointSize(scaledFontPoints(10));
    f.setStyleHint(QFont::SansSerif);
    f.setHintingPreference(QFont::PreferFullHinting); // crisp on Windows
    app->setFont(f);
}

// } // namespace GUIUtil

// ─── ADD TO src/qt/bitcoin.cpp (BEFORE QApplication app(argc, argv)) ────────
//
// #if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
//     QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
//     QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
// #endif
// #if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
//     // Pass fractional scale factors (150%, 175%) through unchanged
//     QApplication::setHighDpiScaleFactorRoundingPolicy(
//         Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
// #endif
//
// // Then, after:  QApplication app(argc, argv);
//     GUIUtil::applyDefaultFont(&app);
//
// ─── ADD TO src/qt/overviewpage.cpp (constructor, after setupUi) ─────────────
//
//     // DPI-aware balance label fonts
//     QFont balFont = ui->labelBalance->font();
//     balFont.setPointSize(GUIUtil::scaledFontPoints(14));
//     balFont.setBold(true);
//     ui->labelBalance->setFont(balFont);
//     ui->labelUnconfirmed->setFont(balFont);
//     ui->labelImmature->setFont(balFont);
//     ui->labelTotal->setFont(balFont);
//
// ─── REPLACE in src/qt/res/styles/light.qss ──────────────────────────────────
// Change every  font-size: Npx  to  font-size: Npt
// Rule of thumb at 96 DPI:  pt = px * 0.75
//
//   QWidget        { font-size: 10pt; }
//   QLabel         { font-size: 10pt; }
//   QTableView,
//   QTreeView      { font-size: 9pt;  }
//   QHeaderView::section { font-size: 9pt; font-weight: bold; }
//   QLineEdit,
//   QTextEdit      { font-size: 10pt; }
//   QPushButton    { font-size: 10pt; }
//   QComboBox      { font-size: 10pt; }
//   QToolTip       { font-size: 9pt;  }
//   QStatusBar     { font-size: 9pt;  }
//
// Large balance display labels — set programmatically via scaledFontPoints(14)
// in overviewpage.cpp rather than in QSS, so they scale with the system DPI.
