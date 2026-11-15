// The reads of one cycle in portions within a device's budgets. MIT.
#include "ReadPortions.hpp"
#include <algorithm>

namespace telemetry::ux::read_portions {
namespace {
// Several struct and array Fields' member reads in one Batch of FieldReads,
// in the order of their first rows, while the Batch's item, request and reply
// budgets allow (each item 8 bytes asked, 12 plus the value answered). A Field
// too large for any Batch, one whose size is unknown, and a Batch that would
// hold one Field stay member reads of their own: one Field's Batch costs the
// same exchange and 14 bytes more. The plan never asks the budget for fewer
// than two candidates.
void batchMembers(Plan& out, const Source& source)
{
	auto& portions = out.portions;
	if (source.individual || !source.readBatch || !source.batchBudget || !source.valueBytes)
		return;
	const auto candidates = std::count_if(portions.begin(), portions.end(), [](const Portion& step) {
		return step.kind == Portion::Kind::Members;
	});
	if (candidates < 2)
		return;
	const auto budget = source.batchBudget();
	if (!budget)
		return;
	const unsigned items = (std::min)(32u, budget->items);
	const unsigned request = (std::min)(batchRequestScratch, budget->requestBytes);
	const unsigned reply = budget->replyBytes;
	if (items < 2 || 2u + 16u > request)
		return;
	std::vector<int> group(portions.size(), -1);
	std::vector<std::vector<std::size_t>> batches;
	int open = -1;
	unsigned answered = 0;
	for (std::size_t at = 0; at < portions.size(); ++at) {
		if (portions[at].kind != Portion::Kind::Members)
			continue;
		const auto bytes = source.valueBytes(portions[at].field);
		if (!bytes || *bytes == 0 || 2u + 12u + *bytes > reply)
			continue;
		const auto n = open >= 0 ? unsigned(batches[std::size_t(open)].size()) : 0u;
		if (open < 0 || n + 1 > items || 2u + 8u * (n + 1) > request || answered + 12u + *bytes > reply) {
			open = int(batches.size());
			batches.emplace_back();
			answered = 2;
		}
		batches[std::size_t(open)].push_back(at);
		answered += 12u + *bytes;
		group[at] = open;
	}
	for (const auto& batch : batches)
		if (batch.size() < 2)
			for (const auto at : batch)
				group[at] = -1;
	if (std::all_of(group.begin(), group.end(), [](int g) { return g < 0; }))
		return;
	std::vector<Portion> joined;
	std::vector<bool> placed(batches.size(), false);
	for (std::size_t at = 0; at < portions.size(); ++at) {
		const auto g = group[at];
		if (g < 0) {
			joined.push_back(std::move(portions[at]));
			continue;
		}
		if (placed[std::size_t(g)])
			continue;
		placed[std::size_t(g)] = true;
		Portion batch;
		batch.kind = Portion::Kind::Batch;
		for (const auto member : batches[std::size_t(g)])
			batch.fields.push_back({portions[member].field, std::move(portions[member].leaves)});
		joined.push_back(std::move(batch));
	}
	portions = std::move(joined);
	out.of.clear();
	for (int at = 0; at < int(portions.size()); ++at) {
		const auto& step = portions[std::size_t(at)];
		switch (step.kind) {
			case Portion::Kind::Many:
				for (const auto id : step.ids)
					out.of.insert(id, at);
				break;
			case Portion::Kind::Field:
			case Portion::Kind::Members:
				out.of.insert(step.field, at);
				break;
			case Portion::Kind::Batch:
				for (const auto& member : step.fields)
					out.of.insert(member.field, at);
				break;
			case Portion::Kind::Fail:
				break;
		}
	}
}
} // namespace

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
		if (source.wholeReads) {
			const auto scalar = source.fieldBytes ? source.fieldBytes(field) : std::nullopt;
			const bool many = limits && source.readMany && scalar && *scalar > 0 && *scalar <= 8 && room(0) &&
			                  2u + 8u + *scalar <= limits->replyBytes;
			if (!many) {
				if (!source.readField && !source.readBatch) {
					failure = Failure::NoFieldRead;
					break;
				}
				Portion whole;
				whole.kind = Portion::Kind::Members;
				whole.field = field;
				out.of.insert(field, int(portions.size()));
				portions.push_back(std::move(whole));
				continue;
			}
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
	batchMembers(out, source);
	if (failure != Failure::None) {
		Portion failed;
		failed.kind = Portion::Kind::Fail;
		failed.failure = failure;
		portions.push_back(std::move(failed));
	}
	return out;
}
} // namespace telemetry::ux::read_portions
