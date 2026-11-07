#include "TranslatorWindow.h"

#include "core/languages.h"

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QComboBox>
#include <QGridLayout>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTextEdit>

namespace lt {

namespace {
const int kPreferredWidth = 980;
const int kPreferredHeight = 560;
/// A window this small stops being two panes with a purpose; past this point
/// it is better to overflow the screen (and let the user scroll/zoom the
/// desktop) than to show a postage stamp.
const QSize kFloor(320, 200);
} // namespace

QSize TranslatorWindow::availableLogicalSize() const
{
    // screen() is null before the window is ever shown, and on X11 the window
    // is placed by the server: windowHandle() may report no screen either.
    // QGuiApplication::primaryScreen() is the honest fallback - the compositor
    // will re-fit the window if it ends up on another monitor.
    QScreen *scr = screen();
    if (!scr) {
        scr = QGuiApplication::primaryScreen();
    }
    if (!scr) {
        return {};
    }
    return scr->availableGeometry().size();
}

QSize TranslatorWindow::fitToScreen(QSize wanted, QSize available)
{
    if (wanted.isEmpty() || available.isEmpty()
        || (wanted.width() <= available.width() && wanted.height() <= available.height())) {
        return wanted;
    }
    // 92%, not 100%: the title bar, the top bar and the window shadows belong
    // to the compositor, and a window exactly the size of the work area is a
    // window whose edges cannot be reached.
    int w = available.width() * 92 / 100;
    int h = int(double(w) * double(wanted.height()) / double(wanted.width()));
    if (h > available.height() * 92 / 100) {
        h = available.height() * 92 / 100;
        w = int(double(h) * double(wanted.width()) / double(wanted.height()));
    }
    // The floor beats the work area on purpose: on a 300x200 sliver (a tiled
    // window, a phone) an overflowing-but-legible two-pane window is worth
    // more than a fully visible stamp, and the desktop can scroll or zoom.
    return QSize(qMax(w, kFloor.width()), qMax(h, kFloor.height()));
}

TranslatorWindow::TranslatorWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Traduku"));
    // The size is a question about the screen, asked per show (see
    // fitToScreen): a window built once at startup would carry whatever the
    // primary screen was then, which is wrong the moment the monitor, the
    // scale or the login session changes.
    resize(fitToScreen(QSize(kPreferredWidth, kPreferredHeight),
                       availableLogicalSize()));

    auto *grid = new QGridLayout(this);
    // Word wrap on every label, and no label holding a row open: a label's
    // minimum width is its longest unbroken word, so the pane captions and the
    // status sentence were what kept this window 787 px wide (measured). A
    // top-level cannot be resized below its layout's minimum, so asking for a
    // smaller window at 200% scaling had no effect and the buttons sat beyond
    // the panel edge - see tests/unit/tst_translatorwindow.cpp.
    auto *srcLabel = new QLabel(QStringLiteral("Original (editable - paste, "
                                               "type; Ctrl+Enter translates)"));
    srcLabel->setWordWrap(true);
    grid->addWidget(srcLabel, 0, 0);
    auto *tgtLabel = new QLabel(QStringLiteral("Translation"));
    tgtLabel->setWordWrap(true);
    grid->addWidget(tgtLabel, 0, 1);

    m_source = new QTextEdit(this);
    m_source->setObjectName(QStringLiteral("m_source"));
    m_source->setReadOnly(false);
    m_source->setLineWrapMode(QTextEdit::WidgetWidth);
    m_source->setAcceptRichText(false);
    grid->addWidget(m_source, 1, 0);

    m_target = new QTextEdit(this);
    m_target->setObjectName(QStringLiteral("m_target"));
    m_target->setReadOnly(true);
    m_target->setLineWrapMode(QTextEdit::WidgetWidth);
    grid->addWidget(m_target, 1, 1);

    connect(m_source, &QTextEdit::textChanged, this, [this]() {
        if (!m_programmaticSource) {
            Q_EMIT sourceEdited();
        }
    });

    m_translateShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return),
                                        this);
    m_translateShortcut->setContext(Qt::WindowShortcut);
    connect(m_translateShortcut, &QShortcut::activated, this,
            &TranslatorWindow::translateRequested);

    auto *bar = new QHBoxLayout;
    m_language = new QComboBox(this);
    m_language->setObjectName(QStringLiteral("m_language"));
    for (const auto &lang : languages()) {
        m_language->addItem(lang.name, lang.code);
    }
    connect(m_language, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_programmaticLang) {
            return;
        }
        Q_EMIT targetLanguagePicked(m_language->currentData().toString());
    });
    m_language->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_language->setMinimumContentsLength(8);   // the list is long; the box is not
    bar->addWidget(m_language);

    auto *settingsBtn = new QPushButton(QStringLiteral("Settings…"), this);
    settingsBtn->setObjectName(QStringLiteral("m_settings"));
    connect(settingsBtn, &QPushButton::clicked,
            this, &TranslatorWindow::settingsRequested);
    bar->addWidget(settingsBtn);

    m_copy = new QPushButton(QStringLiteral("Copy translation"), this);
    m_copy->setObjectName(QStringLiteral("m_copy"));
    m_copy->setEnabled(false);
    connect(m_copy, &QPushButton::clicked, this, [this]() {
        Q_EMIT copyRequested(m_target->toPlainText());
    });
    bar->addWidget(m_copy);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("m_status"));
    m_status->setWordWrap(true);
    m_status->setText(QStringLiteral("Copy text, press Ctrl+C twice - or "
                                     "type here and press Ctrl+Enter."));
    bar->addWidget(m_status, /*stretch*/ 1);

    grid->addLayout(bar, 2, 0, 1, 2);
}

void TranslatorWindow::setTargetLanguage(const QString &code)
{
    const int idx = m_language->findData(code);
    if (idx >= 0 && idx != m_language->currentIndex()) {
        m_programmaticLang = true;
        m_language->setCurrentIndex(idx);
        m_programmaticLang = false;
    }
}

QString TranslatorWindow::targetLanguage() const
{
    return m_language->currentData().toString();
}

QString TranslatorWindow::sourceText() const
{
    return m_source->toPlainText();
}

void TranslatorWindow::setSourceText(const QString &text)
{
    m_programmaticSource = true;
    m_source->setPlainText(text);
    m_programmaticSource = false;
}

void TranslatorWindow::showTargetRows(const QStringList &rows)
{
    m_rows = QVector<QString>(rows.cbegin(), rows.cend());
    m_committed = QVector<bool>(m_rows.size(), false);
    renderTarget();
    updateCopyEnabled();
}

void TranslatorWindow::streamIntoRow(int row, const QString &partial)
{
    if (row < 0 || row >= m_rows.size()) {
        return;   // stale stream after a re-layout
    }
    m_rows[row] = partial;
    renderTarget();
}

void TranslatorWindow::commitRow(int row, const QString &text)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    m_rows[row] = text;
    m_committed[row] = true;
    renderTarget();
    updateCopyEnabled();
}

void TranslatorWindow::renderTarget()
{
    // Re-rendering the whole document is O(n) per delta - fine for
    // document-sized texts, and immune to cursor-position drift while
    // streaming into the middle.
    m_target->setPlainText(m_rows.join(QLatin1Char('\n')));
}

void TranslatorWindow::updateCopyEnabled()
{
    // Enable once the document has any non-placeholder content: with the
    // ledger model, rows start as source placeholders, so "changed" is
    // the signal that real translation landed.
    m_copy->setEnabled(m_committed.contains(true));
}

void TranslatorWindow::statusTranslating(const QString &targetName)
{
    m_status->setText(QStringLiteral("Translating to %1 …").arg(targetName));
}

void TranslatorWindow::statusDone()
{
    m_status->setText(QStringLiteral("Done (%1 characters).")
                          .arg(m_target->toPlainText().size()));
}

void TranslatorWindow::statusFailed(const QString &reason)
{
    m_status->setText(QStringLiteral("Failed: %1").arg(reason));
}

void TranslatorWindow::showForeground()
{
    // Re-ask here, not only in the ctor: the window is built once at startup,
    // while the screen, its scale and the work area can all change before the
    // first translation (monitor hotplug, a login on another seat).
    const QSize fit = fitToScreen(QSize(kPreferredWidth, kPreferredHeight),
                                  availableLogicalSize());
    if (size() != fit) {
        resize(fit);
    }
    showNormal();
    raise();
    activateWindow();
    // The app's own witness that a window came up, with its title and the
    // size it mapped. On Plasma the grid could ask KWin's scripting API for
    // its window list; on GNOME there is no such API and a Wayland client may
    // not enumerate another client's windows at all, so this journal line
    // (the app runs as a user unit -> journalctl --user) plus a QMP
    // framebuffer diff is the DE-independent pair tests/e2e/test_06_window.py
    // asserts on. The title is part of the line for the same reason it is in
    // the KWin/xdotool answers: the witness must say *which* window it saw.
    // Both sizes, because they answer different questions: the logical one is
    // what the layout was asked to fill, the device one is what lands on the
    // panel - and it is the device one that must not exceed the screen at
    // 200% scaling. tests/e2e/test_09 greps the px pair.
    const qreal dpr = devicePixelRatioF();
    qInfo("traduku: window shown \"%s\" %dx%d (%dx%d px)",
          qUtf8Printable(windowTitle()), width(), height(),
          int(width() * dpr), int(height() * dpr));
}

} // namespace lt
