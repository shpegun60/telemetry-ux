// Review probe (architect, 2026-11-11). Read-only reuse of the Static contract
// fixtures: does an ordinary Watchlist edit in the Static provider keep the
// Trend's selected Field, as the Dynamic WatchlistModel's row insertion does?
// Build: see CMakeLists.txt beside this file. Prints facts; exit 0 = no defect.
#define main telemetry_static_contracts_main
#include "../../../static_qml_configurator/Contracts.cpp"
#undef main
#include <cstdio>

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	Api api;
	api.watched[0] = QStringLiteral("42");
	api.watched[1] = QStringLiteral("7");
	StaticConfiguratorProvider p(watchBinding(api, 2));
	p.selectEndpoint(endpoint(p, 0));
	const bool firstAdded = p.addWatch();
	TrendHistory trend;
	trend.setSource(p.watchlist());
	trend.setRow(0);
	p.refreshWatch();
	api.finish(true);
	watchEvents();
	const auto before = trend.points().size();
	std::printf("before: added=%d row=%d points=%lld state=%s\n", firstAdded, trend.row(),
	            static_cast<long long>(before), qPrintable(trend.state()));

	// The user adds a second Field to the Watchlist while Field0 is being trended.
	p.selectEndpoint(endpoint(p, 1));
	const bool secondAdded = p.addWatch();
	std::printf("after addWatch: added=%d row=%d points=%lld state=%s\n", secondAdded,
	            trend.row(), static_cast<long long>(trend.points().size()),
	            qPrintable(trend.state()));

	// The next completed reading of Field0 no longer reaches the trend.
	p.refreshWatch();
	api.finish(true);
	watchEvents();
	std::printf("after next reading: row=%d points=%lld state=%s\n", trend.row(),
	            static_cast<long long>(trend.points().size()), qPrintable(trend.state()));
	const bool kept = trend.row() == 0;
	std::printf("VERDICT static-watch-edit-drops-trend-selection %s\n", kept ? "ABSENT" : "PRESENT");
	return kept ? 0 : 1;
}
