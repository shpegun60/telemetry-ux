// The reads of one cycle in portions within a device's budgets. MIT.
#include "ReadPortions.hpp"
#include <algorithm>

namespace telemetry::ux::read_portions {
// The budgets and failures whole Fields always had: a list of whole Fields
// sends exactly the reads it did before the plan was shared.
Plan plan(std::span<const WatchKey> keys, const Source& source)
{
	Plan out;
	auto& portions = out.portions;
	Failure failure = Failure::None;
	std::optional<Budget> limits;
	bool asked = false;
	int open = -1;
	unsigned reply = 0;
	const auto room = [&limits](unsigned n) {
		return n < 32u && n < limits->items && 2u + 4u * (n + 1u) <= limits->requestBytes;
	};
	for (const auto key : keys) {
		if (watch_key::isUnresolved(key))
			continue;
		const auto field = watch_key::field(key);
		if (!watch_key::isWhole(key)) {
			out.members = true;
			if (!source.readMembers) {
				failure = Failure::NoMembers;
				break;
			}
			auto at = out.of.value(field, -1);
			if (at < 0 || portions[std::size_t(at)].kind != Portion::Kind::Members) {
				at = int(portions.size());
				Portion members;
				members.kind = Portion::Kind::Members;
				members.field = field;
				portions.push_back(std::move(members));
				out.of.insert(field, at);
			}
			portions[std::size_t(at)].leaves.push_back(watch_key::ordinal(key));
			continue;
		}
		if (source.individual) {
			if (!source.readField) {
				failure = Failure::NoFieldRead;
				break;
			}
			Portion one;
			one.kind = Portion::Kind::Field;
			one.field = field;
			out.of.insert(field, int(portions.size()));
			portions.push_back(std::move(one));
			continue;
		}
		if (!asked) {
			limits = source.budget ? source.budget() : std::nullopt;
			asked = true;
		}
		if (!limits || !source.readMany) {
			failure = Failure::NoReadMany;
			break;
		}
		// A ReadMany portion takes Fields while its item, request and reply
		// budgets allow; a Field it can't take starts the next one.
		const bool fits = open >= 0 && room(unsigned(portions[std::size_t(open)].ids.size()));
		const auto bytes = source.fieldBytes ? source.fieldBytes(field) : std::nullopt;
		if (!bytes || *bytes == 0 || *bytes > 8) {
			if (fits) {
				// The read it would have joined is not sent either.
				portions.resize(std::size_t(open));
				for (auto it = out.of.begin(); it != out.of.end();)
					it = it.value() >= open ? out.of.erase(it) : std::next(it);
			}
			failure = fits || room(0) ? Failure::NotScalar : Failure::ExceedsReadMany;
			break;
		}
		if (!fits || reply + 8u + *bytes > limits->replyBytes) {
			if (!room(0) || 2u + 8u + *bytes > limits->replyBytes) {
				failure = Failure::ExceedsReadMany;
				break;
			}
			open = int(portions.size());
			portions.push_back({});
			reply = 2;
		}
		portions[std::size_t(open)].ids.push_back(field);
		reply += 8u + *bytes;
		out.of.insert(field, open);
	}
	for (auto& step : portions)
		std::sort(step.leaves.begin(), step.leaves.end());
	if (failure != Failure::None) {
		Portion failed;
		failed.kind = Portion::Kind::Fail;
		failed.failure = failure;
		portions.push_back(std::move(failed));
	}
	return out;
}
} // namespace telemetry::ux::read_portions
