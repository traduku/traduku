#include "aligner.h"

#include <QRegularExpression>
#include <cmath>

namespace lt {

namespace {

int unitLen(const QString &s)
{
    // Content measure robust to script/word-length differences: count
    // words, not characters (char counts differ ~3x between RU/EN/DE for
    // identical content; word counts track much closer).
    const QString t = s.trimmed();
    if (t.isEmpty()) {
        return 0;
    }
    return t.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
}

double matchCost(int a, int b)
{
    const double la = qMax(1, a), lb = qMax(1, b);
    return std::fabs(std::log(la / lb)) / 2.0;
}

QString joinRange(const QVector<Chunk> &chunks, int from, int to)   // [from,to)
{
    QString out;
    for (int j = from; j < to; ++j) {
        out += chunks.at(j).text;
        if (j + 1 < to) {
            out += chunks.at(j).sep;
        }
    }
    return out.trimmed();
}

} // namespace

QVector<QString> alignChunks(const QVector<Chunk> &src, const QVector<Chunk> &tgt)
{
    QVector<int> sc;   // content indices into src
    for (int i = 0; i < src.size(); ++i) {
        if (!src.at(i).text.trimmed().isEmpty()) {
            sc.append(i);
        }
    }
    QVector<Chunk> tc;
    for (const Chunk &c : tgt) {
        if (!c.text.trimmed().isEmpty()) {
            tc.append(c);
        }
    }
    const int n = sc.size();
    const int m = tc.size();
    if (n == 0 || m == 0) {
        return {};
    }

    QVector<int> slen(n), tlen(m);
    for (int i = 0; i < n; ++i) {
        slen[i] = unitLen(src.at(sc.at(i)).text);
    }
    for (int j = 0; j < m; ++j) {
        tlen[j] = unitLen(tc.at(j).text);
    }

    constexpr double GapSource = 0.60, GapTarget = 0.50, MaxAvgCost = 0.50;

    // dp[i][j]: minimal cost to align src[i..] with tgt[j..].
    QVector<QVector<double>> dp(n + 1, QVector<double>(m + 1, 0.0));
    for (int i = n; i >= 0; --i) {
        for (int j = m; j >= 0; --j) {
            if (i == n && j == m) {
                continue;
            }
            double best = 1e18;
            if (i < n && j < m) {
                best = std::min(best, dp[i + 1][j + 1] + matchCost(slen.at(i), tlen.at(j)));
            }
            // Hunalign-style tail anchor: the last content source slot is
            // never dropped while target chunks remain - merged runs put
            // their blanks at the head, the tail keeps positional 1:1.
            if (i < n && !(i == n - 1 && j < m)) {
                // epsilon gradient: among equal-cost paths, drop source
                // chunks as late as possible -> merged translations land
                // on the FIRST slot of their run, blanks trail.
                best = std::min(best, dp[i + 1][j] + GapSource + 1e-3 * (n - 1 - i));
            }
            if (j < m) {
                best = std::min(best, dp[i][j + 1] + GapTarget);
            }
            dp[i][j] = best;
        }
    }
    if (dp[0][0] / std::max(n, m) > MaxAvgCost) {
        return {};
    }

    QVector<QString> result(src.size());
    int i = 0, j = 0, runStart = 0;
    while (i < n && j < m) {
        const double cur = dp[i][j];
        const auto onPath = [&](double c) { return c <= cur + 1e-9; };
        const double costMatch = dp[i + 1][j + 1] + matchCost(slen.at(i), tlen.at(j));
        const double costDropT = dp[i][j + 1] + GapTarget;
        if (onPath(costMatch)) {
            result[sc.at(i)] = joinRange(tc, runStart, j + 1);
            runStart = j + 1;
            ++i;
            ++j;
        } else if (onPath(costDropT)) {
            ++j;   // extra target line folds into the current run
        } else {   // dropped source chunk: empty translation
            result[sc.at(i)] = QStringLiteral("");
            ++i;
        }
        // (dropS at the last content slot with target left is off-path by
        // construction, so the walk can never hit it)
    }
    if (j < m) {   // trailing target text: append to the last matched slot
        const QString tail = joinRange(tc, runStart, m);
        for (int k = n - 1; k >= 0; --k) {
            if (!result.at(sc.at(k)).isEmpty()) {
                result[sc.at(k)] += QLatin1Char('\n') + tail;
                break;
            }
        }
    }
    for (; i < n; ++i) {
        result[sc.at(i)] = QStringLiteral("");   // untranslated tail
    }

    // CAT convention: a merged translation sits on the FIRST slot of its
    // run, blanks trail it. DP ties may put the blank anywhere in the run;
    // bubble non-empty texts leftwards across blanks (content slots only -
    // null QStrings are separator slots and never move).
    for (int k = 1; k < n; ++k) {
        for (int p = k; p > 0; --p) {
            const int a = sc.at(p - 1), b = sc.at(p);
            if (result.at(a).isEmpty() && !result.at(a).isNull()
                && !result.at(b).isEmpty()) {
                result[a] = result.at(b);
                result[b] = QStringLiteral("");
            }
        }
    }

    // Structural sanity: if most content slots ended up untranslated, the
    // "alignment" is fiction - force the per-chunk fallback.
    int empties = 0;
    for (int k = 0; k < n; ++k) {
        if (result.at(sc.at(k)).isEmpty()) {
            ++empties;
        }
    }
    if (empties * 2 > n) {
        return {};
    }
    return result;
}

} // namespace lt
