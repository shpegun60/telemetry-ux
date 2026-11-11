// PR #48 review probe (telemetry-ux feat/watchlist-trend at 4e0c1d6). Prints
// observations; exits 0. Built by CMakeLists.txt beside it against a checkout.
#include <Telemetry/UX/TrendHistory.hpp>
#include <Telemetry/UX/WatchSample.hpp>
#include <Telemetry/UX/WatchlistModel.hpp>
#include <QtCore/QAbstractListModel>
#include <QtCore/QCoreApplication>
#include <climits>
#include <limits>
#include <cstdio>

using telemetry::ux::TrendHistory;
using telemetry::ux::WatchSample;
using telemetry::ux::WatchlistModel;

namespace {
void print(const char* label, const TrendHistory& h)
{
	const auto last = h.points().isEmpty() ? QVariantMap{} : h.points().last().toMap();
	std::printf("%s: points=%d last.value=%s breakBefore=%d state=\"%s\"\n", label,
	            int(h.points().size()), qPrintable(last.value("value").toString()),
	            last.value("breakBefore").toBool(), qPrintable(h.state()));
}

// A host model that documents every role but leaves statusCode unset (invalid)
// for a status it has no code for.
class Partial : public QAbstractListModel {
public:
	enum { Status = Qt::UserRole + 1, Code, Revision, Epoch, Text, Value, Exact };
	QString status = QStringLiteral("NotRead"), revision = QStringLiteral("0");
	QVariant code = 0;
	double value = 0;
	int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 1; }
	QVariant data(const QModelIndex& index, int role) const override
	{
		if (!index.isValid())
			return {};
		switch (role) {
			case Status: return status;
			case Code: return code;
			case Revision: return revision;
			case Epoch: return QStringLiteral("1");
			case Text: return QString::number(value);
			case Value: return status == QStringLiteral("Ok") ? QVariant(value) : QVariant();
			case Exact: return status == QStringLiteral("Ok");
			default: return {};
		}
	}
	QHash<int, QByteArray> roleNames() const override
	{
		return {{Status, "status"}, {Code, "statusCode"}, {Revision, "sampleRevision"},
		        {Epoch, "sampleEpoch"}, {Text, "sampleText"}, {Value, "sampleValue"},
		        {Exact, "sampleExact"}};
	}
	void publish(QString s, QVariant c, QString r, double v)
	{
		status = s; code = c; revision = r; value = v;
		Q_EMIT dataChanged(index(0, 0), index(0, 0));
	}
};
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	// P1: the public setResult() keeps a defaulted sample. A caller written
	// against the old signature (examples/inspector/InspectorController.cpp:1633)
	// still compiles and publishes Ok rows that a Trend refuses to plot.
	WatchlistModel model;
	model.bind([] { return true; });
	model.add(1, QStringLiteral("Voltage"));
	TrendHistory trend;
	trend.setSource(&model);
	trend.setRow(0);
	model.setResult(0, QStringLiteral("Ok"), QStringLiteral("4.25"));
	model.setResult(0, QStringLiteral("Ok"), QStringLiteral("4.5"));
	print("P1 setResult without sample", trend);

	// P2: an invalid statusCode reads as 0 = NotRead, the one code that never
	// breaks the line, so a failure between two readings leaves no gap.
	Partial partial;
	TrendHistory gapTrend;
	gapTrend.setSource(&partial);
	gapTrend.setRow(0);
	partial.publish(QStringLiteral("Ok"), 2, QStringLiteral("1"), 1.0);
	partial.publish(QStringLiteral("ReadError"), QVariant(), QStringLiteral("1"), 1.0);
	partial.publish(QStringLiteral("Ok"), 2, QStringLiteral("2"), 2.0);
	print("P2 failure with invalid statusCode", gapTrend);

	// P3: exactness limits of WatchSample.
	const long long limit = 9007199254740991LL;
	std::printf("P3 ofSigned(+limit)=%d ofSigned(-limit)=%d ofSigned(limit+1)=%d "
	            "ofSigned(LLONG_MIN)=%d ofUnsigned(limit)=%d ofUnsigned(limit+1)=%d "
	            "ofUnsigned(ULLONG_MAX)=%d ofFloating(inf)=%d ofFloating(-0.0)=%d\n",
	            WatchSample::ofSigned(limit).exact, WatchSample::ofSigned(-limit).exact,
	            WatchSample::ofSigned(limit + 1).exact, WatchSample::ofSigned(LLONG_MIN).exact,
	            WatchSample::ofUnsigned(limit).exact, WatchSample::ofUnsigned(limit + 1).exact,
	            WatchSample::ofUnsigned(ULLONG_MAX).exact,
	            WatchSample::ofFloating(std::numeric_limits<double>::infinity()).exact,
	            WatchSample::ofFloating(-0.0).exact);
	return 0;
}
