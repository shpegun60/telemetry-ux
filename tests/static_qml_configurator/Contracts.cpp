/* Qt presentation contracts over an explicit fake API facade, not SDK execution.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include <Telemetry/UX/StaticConfiguratorProvider.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <Telemetry/UX/WatchSample.hpp>
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEvent>
#include <QPersistentModelIndex>
#include <QTimer>
#include <array>
#include <cstdio>
#include <functional>
#include <utility>

using namespace telemetry::ux;
static unsigned checks = 0, failures = 0, initializations = 0;

static void check(bool value, unsigned line)
{
	++checks;
	if (!value) {
		++failures;
		std::fprintf(stderr, "Contracts.cpp:%u\n", line);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

struct Api final : QObject {
	struct Subscription {
		QPointer<QObject> context;
		std::function<void()> changed;
		std::function<void(QString, QVariantMap)> finished;
	};

	QVector<Subscription> subscriptions;
	bool ready = true, valid = true, refuse = false;
	quint32 epoch = 1;
	unsigned issued = 0;
	QVariant value = quint32{17}, argument;
	QByteArray payload = "seed17", original;
	QString token;
	QStringList calls;
	QVector<QVariant> watched{QStringLiteral("18446744073709551615"),
	                          QStringLiteral("-9223372036854775808"), QStringLiteral("-77")};

	void changed()
	{
		const auto copy = subscriptions;
		for (const auto& slot : copy)
			if (slot.context)
				slot.changed();
	}

	void finish(bool success, const QString& error = {}, bool accepted = false,
	            QVariant response = {}, int dispatch = 0)
	{
		const auto completed = token;
		const QVariantMap result{{"success", success}, {"accepted", accepted},
		                         {"errorKind", error}, {"message", QString()},
		                         {"value", response},  {"dispatchStatus", dispatch},
		                         {"endpointStatus", 0}};
		const auto copy = subscriptions;
		for (const auto& slot : copy)
			if (slot.context)
				slot.finished(completed, result);
	}

	QString issue(const QString& operation, const QVariant& input = {}, const QByteArray& seed = {})
	{
		if (refuse)
			return {};
		calls.append(operation);
		argument = input;
		original = seed;
		token = QString::number(++issued);
		return token;
	}

	void invalidate()
	{
		ready = false;
		valid = false;
		++epoch;
		changed();
	}
};

static StaticConfiguratorBinding binding(Api& api, int category = 1, bool noArgument = false,
                                         quint64 arrayCount = 0)
{
	StaticConfiguratorBinding b;
	b.api = &api;
	StaticType empty;
	empty.id = 0;
	b.types.append(empty);
	StaticType scalar;
	scalar.id = 1;
	scalar.kind = StaticTypeKind::Scalar;
	scalar.scalarCode = 6;
	b.types.append(scalar);
	StaticType array;
	array.id = 2;
	array.kind = StaticTypeKind::Array;
	array.elementTypeId = 1;
	array.count = arrayCount;
	b.types.append(array);
	StaticEndpoint e;
	e.category = category;
	e.id = 0;
	e.catalog = "control";
	e.name = "Test";
	e.valueType = arrayCount ? 2 : 1;
	e.requestType = noArgument ? 0 : 1;
	e.responseType = 1;
	e.readable = e.writable = category == 1;
	e.cached = +[](QObject* object) {
		return static_cast<Api*>(object)->value;
	};
	e.cachedValid = +[](QObject* object) {
		return static_cast<Api*>(object)->valid;
	};
	e.cachedPayload = +[](QObject* object) {
		return static_cast<Api*>(object)->payload;
	};
	if (!noArgument)
		e.defaultRequest = +[]() -> QVariant {
			++initializations;
			return quint32{0};
		};
	e.validate = +[](const QVariant& value, QString& error) {
		bool ok = false;
		const auto number = value.toLongLong(&ok);
		if (!ok || number < 0 || number > UINT32_MAX) {
			error = "Out of range";
			return false;
		}
		return true;
	};
	e.read = +[](QObject* object) {
		return static_cast<Api*>(object)->issue("read");
	};
	e.writePreserving = +[](QObject* object, const QVariant& value, const QByteArray& seed) {
		return static_cast<Api*>(object)->issue("write", value, seed);
	};
	e.invoke = +[](QObject* object, const QVariant& value) {
		return static_cast<Api*>(object)->issue("invoke", value);
	};
	b.endpoints.append(e);
	b.ready = +[](QObject* object) {
		return static_cast<Api*>(object)->ready;
	};
	b.session = +[](QObject* object) {
		return static_cast<Api*>(object)->epoch;
	};
	b.connect = +[](QObject* object, QObject* context, std::function<void()> changed,
	                std::function<void(QString, QVariantMap)> finished) {
		static_cast<Api*>(object)->subscriptions.append(
		    {context, std::move(changed), std::move(finished)});
	};
	return b;
}

static QModelIndex endpoint(StaticConfiguratorProvider& provider, int row = 0)
{
	auto* model = provider.endpoints();
	return model->index(row, 0, model->index(0, 0));
}

static int role(QAbstractItemModel* model, const QByteArray& name)
{
	const auto names = model->roleNames();
	for (auto it = names.cbegin(); it != names.cend(); ++it)
		if (it.value() == name)
			return it.key();
	return -1;
}

static QVariantList arrayValue(int count)
{
	QVariantList result;
	result.reserve(count);
	for (int i = 0; i < count; ++i)
		result.append(i);
	return result;
}

static void actions()
{
	for (const int category : {2, 3}) {
		Api api;
		StaticConfiguratorProvider p(binding(api, category, true));
		p.setAllowMutations(true);
		p.selectEndpoint(endpoint(p));
		CHECK(!p.editing());
		CHECK(category == 2 ? p.canExecute() : p.canCallService());
		if (category == 2)
			p.executeCommand();
		else
			p.callService();
		CHECK(p.busy());
		CHECK(api.calls == QStringList{"invoke"});
		api.finish(category != 2, {}, category == 2, quint32{42});
		CHECK(!p.busy());
		CHECK(category == 2 ? p.commandOutcome().startsWith("Accepted")
		                    : p.serviceOutcome() == "Completed; value available");
		CHECK(category == 2 ? p.canExecute() : p.canCallService());
		if (category == 2) {
			p.cancelEdit();
			CHECK(p.canExecute());
			p.executeCommand();
			api.finish(true);
			CHECK(p.commandOutcome() == "Executed");
		} else {
			CHECK(p.responses()->rowCount() == 1);
			CHECK(p.responses()
			          ->data(p.responses()->index(0, 0), role(p.responses(), "value"))
			          .toString() == "42");
		}
	}
	Api api;
	StaticConfiguratorProvider p(binding(api, 2));
	p.setAllowMutations(true);
	p.selectEndpoint(endpoint(p));
	CHECK(!p.editing());
	CHECK(p.canExecute());
	p.cancelEdit();
	CHECK(p.canExecute());
	p.executeCommand();
	api.finish(false, "OutcomeUnknown");
	CHECK(p.commandOutcome().contains("OutcomeUnknown"));
	CHECK(!p.canExecute());
	api.invalidate();
	api.ready = true;
	++api.epoch;
	api.changed();
	CHECK(api.calls.size() == 1); // No automatic repeat after new admission.
}

static void selectionIdentity()
{
	Api api;
	auto b = binding(api, 1);
	// These distinct endpoints deliberately have the same ID, catalog, and name.
	// Only their category differs; display text cannot be used as identity.
	auto command = binding(api, 2, true).endpoints[0];
	b.endpoints.append(command);
	StaticConfiguratorProvider p(std::move(b));
	auto* model = p.endpoints();
	const auto field = model->index(0, 0, model->index(0, 0));
	const auto action = model->index(0, 0, model->index(1, 0));
	CHECK(field.isValid() && action.isValid());
	CHECK(!p.selectedEndpointIndex().isValid());
	p.selectEndpoint(action);
	CHECK(p.selectedEndpointIndex() == action && p.commandSelected());
	p.selectEndpoint(field);
	CHECK(p.selectedEndpointIndex() == field && !p.commandSelected());
	CHECK(api.issued == 0);
	p.refresh();
	CHECK(p.busy());
	p.selectEndpoint(action); // Busy selection is refused without moving the highlight.
	CHECK(p.selectedEndpointIndex() == field);
	api.finish(true);
	api.invalidate();
	// Static selection remains a valid part of the fixed schema while offline.
	CHECK(p.selectedEndpointIndex() == field);
}

static void draftAndReadback()
{
	Api api;
	StaticConfiguratorProvider p(binding(api));
	p.setAllowMutations(true);
	p.selectEndpoint(endpoint(p));
	CHECK(p.canEdit());
	p.beginEdit();
	CHECK(p.editing());
	auto* model = p.values();
	const auto old = model->index(0, 0);
	CHECK(!p.setScalarText(old, "bad"));
	CHECK(!p.canApply());
	CHECK(model->data(old, role(model, "editorText")).toString() == "bad");
	CHECK(p.setScalarText(old, "18"));
	CHECK(p.canApply());
	p.apply();
	CHECK(p.busy());
	CHECK(api.argument.toUInt() == 18);
	CHECK(api.original == "seed17");
	api.finish(false, "QueueFull");
	CHECK(p.editing());
	CHECK(p.canApply());
	CHECK(model->data(model->index(0, 0), role(model, "editorText")).toString() == "18");
	CHECK(api.calls == QStringList{"write"});
	p.apply();
	CHECK(p.busy());
	api.valid = false;
	api.changed();
	api.finish(true);
	CHECK(p.busy());
	CHECK(!p.editing());
	CHECK(p.writeOutcome().startsWith("Applied"));
	CHECK(api.calls == QStringList({"write", "write", "read"}));
	unsigned reads = 0;
	bool published = true;
	QObject::connect(&p, &StaticConfiguratorProvider::readFinished, [&](bool value) {
		++reads;
		published = value;
	});
	api.invalidate();
	api.finish(true, {}, false, quint32{10});
	CHECK(!p.busy());
	CHECK(!p.connected());
	CHECK(!published && reads == 1);
	CHECK(model->rowCount() == 0);
	CHECK(!model->data(old, Qt::DisplayRole).isValid());
	CHECK(p.writeOutcome().startsWith("Applied"));
	CHECK(api.calls.size() == 3);
	api.ready = true;
	++api.epoch;
	api.changed();
	CHECK(api.calls.size() == 3);
	CHECK(!p.canEdit());
	p.refresh();
	CHECK(api.calls.last() == "read");
	api.value = quint32{10};
	api.payload = "seed10";
	api.valid = true;
	api.changed();
	api.finish(true);
	CHECK(!p.allowMutations() && !p.canEdit());
	p.setAllowMutations(true);
	CHECK(p.canEdit());
	CHECK(reads == 2 && published);
	CHECK(model->data(model->index(0, 0), role(model, "value")).toString() == "10");
	p.beginEdit();
	CHECK(!p.setScalarText(old, "99")); // Old index never addresses a new draft.
	const auto fresh = model->index(0, 0);
	CHECK(p.setScalarText(fresh, "11"));
	api.refuse = true;
	p.apply();
	CHECK(!p.busy());
	CHECK(p.editing());
	CHECK(p.canApply());
	CHECK(model->data(fresh, role(model, "editorText")).toString() == "11");
}

static void sessionPermission()
{
	for (const int category : {1, 2, 3}) {
		Api api;
		StaticConfiguratorProvider p(binding(api, category, category != 1));
		p.setAllowMutations(true);
		CHECK(p.allowMutations());
		api.changed();
		CHECK(p.allowMutations()); // Ordinary cache publication preserves opt-in.
		api.invalidate();
		CHECK(!p.allowMutations());
		p.setAllowMutations(true);
		CHECK(!p.allowMutations()); // Disconnected opt-in cannot arm a future session.
		api.ready = api.valid = true;
		++api.epoch;
		api.changed();
		p.selectEndpoint(endpoint(p));
		CHECK(p.connected() && !p.allowMutations());
		CHECK(!p.canEdit() && !p.canApply() && !p.canExecute() && !p.canCallService());
		p.beginEdit();
		p.apply();
		p.executeCommand();
		p.callService();
		CHECK(api.calls.isEmpty());
		p.setAllowMutations(true);
		CHECK(p.allowMutations());
		CHECK(category == 1 ? p.canEdit() : category == 2 ? p.canExecute() : p.canCallService());
		++api.epoch; // Direct replacement also clears permission, without a not-ready event.
		api.changed();
		CHECK(!p.allowMutations());
		CHECK(!p.canEdit() && !p.canApply() && !p.canExecute() && !p.canCallService());
		CHECK(api.calls.isEmpty());
	}
}

static void responseMutationLifetime()
{
	for (const int category : {1, 2, 3}) {
		for (const auto& completion :
		     {QString(), QStringLiteral("Endpoint"), QStringLiteral("OutcomeUnknown")}) {
			Api api;
			auto b = binding(api, 3, true);
			b.endpoints[0].name = "Inspect";
			auto actor = binding(api, category, category != 1).endpoints[0];
			actor.id = 1;
			actor.name = "Actor";
			b.endpoints.append(actor);
			StaticConfiguratorProvider p(std::move(b));
			p.setAllowMutations(true);
			p.selectEndpoint(endpoint(p));
			p.callService();
			api.finish(true, {}, false, quint32{42});
			auto* responses = p.responses();
			const auto prior = responses->index(0, 0);
			const auto responseValue = [&] {
				return responses->data(responses->index(0, 0), role(responses, "value")).toString();
			};
			CHECK(responseValue() == "42");
			p.selectEndpoint(category == 3
			                     ? endpoint(p, 1)
			                     : p.endpoints()->index(0, 0, p.endpoints()->index(1, 0)));
			if (category == 1)
				p.beginEdit();
			const auto submit = [&] {
				if (category == 1)
					p.apply();
				else if (category == 2)
					p.executeCommand();
				else
					p.callService();
			};
			api.refuse = true;
			submit();
			CHECK(!p.busy() && responseValue() == "42");
			CHECK(!p.serviceResponseEndpoint().isEmpty());
			api.refuse = false;
			for (const auto& refusal : {QStringLiteral("QueueFull"), QStringLiteral("Busy")}) {
				submit();
				CHECK(p.busy() && responses->rowCount() == 0);
				CHECK(responses->data(prior, role(responses, "value")).toString().isEmpty());
				QMetaObject::invokeMethod(
				    &api,
				    [&api, refusal] {
					    api.finish(false, refusal);
				    },
				    Qt::QueuedConnection);
				QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
				CHECK(!p.busy() && responseValue() == "42");
				CHECK(!p.serviceResponseEndpoint().isEmpty());
			}
			submit();
			CHECK(p.busy() && responses->rowCount() == 0);
			api.finish(completion.isEmpty(), completion, false, quint32{99});
			if (category == 1 && completion.isEmpty())
				api.finish(true); // Explicit readback after confirmed Write.
			CHECK(!p.busy());
			if (category == 3 && completion.isEmpty()) {
				CHECK(responseValue() == "99");
			} else {
				CHECK(responses->rowCount() == 0 && p.serviceResponseEndpoint().isEmpty());
			}
			api.invalidate();
			CHECK(responseValue().isEmpty());
			CHECK(!responses->data(prior, role(responses, "value")).isValid());
			CHECK(api.issued == (category == 1 && completion.isEmpty() ? 5u : 4u));
		}
	}
}

static void paging()
{
	Api api;
	api.value = arrayValue(300);
	StaticConfiguratorProvider p(binding(api, 1, false, 300));
	p.selectEndpoint(endpoint(p));
	auto* model = p.values();
	CHECK(model->rowCount() == 128);
	CHECK(model->canFetchMore({}));
	CHECK(!model->hasChildren(model->index(0, 0)));
	QVector<QPair<int, int>> inserted;
	QObject::connect(model, &QAbstractItemModel::rowsInserted,
	                 [&](const QModelIndex& parent, int first, int last) {
		                 CHECK(!parent.isValid());
		                 inserted.append({first, last});
	                 });
	model->fetchMore({});
	CHECK(model->rowCount() == 256);
	CHECK(model->canFetchMore({}));
	model->fetchMore({});
	CHECK(model->rowCount() == 300);
	CHECK(!model->canFetchMore({}));
	CHECK(inserted == QVector<QPair<int, int>>({{128, 255}, {256, 299}}));
	CHECK(model->data(model->index(299, 0), role(model, "value")).toString() == "299");
	const QPersistentModelIndex saved(model->index(255, 0));
	api.invalidate();
	CHECK(!saved.isValid());
	CHECK(model->rowCount() == 0);
	CHECK(!model->canFetchMore({}));
}

static void reentrantInsert()
{
	Api api;
	api.value = arrayValue(300);
	StaticConfiguratorProvider p(binding(api, 1, false, 300));
	p.setAllowMutations(true);
	p.selectEndpoint(endpoint(p));
	auto* model = p.values();
	const auto old = model->index(0, 0);
	QPersistentModelIndex persistent(old);
	unsigned nested = 0, resets = 0;
	QObject::connect(model, &QAbstractItemModel::modelReset, [&] {
		++resets;
	});
	QObject::connect(model, &QAbstractItemModel::rowsAboutToBeInserted,
	                 [&](const QModelIndex&, int first, int last) {
		                 ++nested;
		                 CHECK(first == 128 && last == 255);
		                 p.setAllowMutations(false);
		                 CHECK(p.allowMutations());
		                 p.beginEdit();
		                 CHECK(!p.editing());
		                 model->fetchMore({});
		                 api.invalidate();
		                 CHECK(model->data(old, role(model, "value")).toString().isEmpty());
		                 CHECK(model->data(old, role(model, "value")).metaType().id() ==
		                       QMetaType::QString);
	                 });
	model->fetchMore({});
	QCoreApplication::processEvents();
	CHECK(nested == 1);
	CHECK(resets >= 1);
	CHECK(model->rowCount() == 0);
	CHECK(!persistent.isValid());
	CHECK(!p.connected());
	CHECK(api.calls.isEmpty());
}

static void destruction()
{
	for (int event = 0; event < 6; ++event) {
		Api api;
		api.value = arrayValue(300);
		QPointer<StaticConfiguratorProvider> p =
		    new StaticConfiguratorProvider(binding(api, 1, false, 300));
		if (event < 4) {
			auto* model = event < 2 ? p->values() : p->responses();
			if (event % 2 == 0)
				QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, [p] {
					p->deleteLater();
				});
			else
				QObject::connect(model, &QAbstractItemModel::modelReset, [p] {
					p->deleteLater();
				});
			p->selectEndpoint(endpoint(*p));
		} else {
			p->selectEndpoint(endpoint(*p));
			auto* model = p->values();
			if (event == 4)
				QObject::connect(model, &QAbstractItemModel::rowsAboutToBeInserted, [p] {
					p->deleteLater();
				});
			else
				QObject::connect(model, &QAbstractItemModel::rowsInserted, [p] {
					p->deleteLater();
				});
			model->fetchMore({});
		}
		CHECK(p);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		CHECK(!p);
		api.changed();
		CHECK(api.calls.isEmpty());
	}
	for (const bool onRead : {false, true}) {
		Api api;
		QPointer<StaticConfiguratorProvider> p = new StaticConfiguratorProvider(binding(api));
		if (onRead) {
			p->selectEndpoint(endpoint(*p));
			QObject::connect(p, &StaticConfiguratorProvider::readFinished, [p] {
				delete p;
			});
			p->refresh();
			api.finish(true);
		} else {
			QObject::connect(p, &StaticConfiguratorProvider::stateChanged, [p] {
				delete p;
			});
			p->setAllowMutations(true);
		}
		CHECK(!p);
		api.changed();
		CHECK(api.calls.size() == (onRead ? 1 : 0));
	}
}

static void budgetsAndForeignIndices()
{
	Api api;
	auto huge = binding(api, 2);
	huge.endpoints[0].requestType = 2;
	huge.types[2].count = 65537;
	initializations = 0;
	StaticConfiguratorProvider p(std::move(huge));
	p.setAllowMutations(true);
	p.selectEndpoint(endpoint(p));
	CHECK(initializations == 0);
	CHECK(!p.canExecute());
	CHECK(p.values()->rowCount() == 0);
	CHECK(p.statusText().contains("limits"));
	Api other;
	StaticConfiguratorProvider q(binding(other));
	q.setAllowMutations(true);
	const auto own = endpoint(q);
	CHECK(!q.selectedEndpointIndex().isValid());
	q.selectEndpoint(endpoint(p));
	CHECK(q.selection().isEmpty() && !q.selectedEndpointIndex().isValid());
	q.selectEndpoint(own);
	CHECK(q.selectedEndpointIndex() == own);
	q.beginEdit();
	CHECK(q.editing());
	CHECK(!q.setScalarText(p.endpoints()->index(0, 0), "42"));
	CHECK(!q.setScalarText(q.values()->index(0, 1), "42"));
	CHECK(q.canApply());
	StaticConfiguratorBinding deep = binding(api, 2);
	deep.endpoints[0].requestType = 3;
	for (quint32 i = 3; i < 70; ++i) {
		StaticType type;
		type.id = i;
		type.kind = StaticTypeKind::Struct;
		type.members.append({"nested", i == 69 ? quint32{1} : i + 1});
		deep.types.append(type);
	}
	initializations = 0;
	StaticConfiguratorProvider nested(std::move(deep));
	nested.setAllowMutations(true);
	nested.selectEndpoint(endpoint(nested));
	CHECK(initializations == 0);
	CHECK(!nested.canExecute());
}

static void readRefusalsAndQueuedReset()
{
	Api api;
	StaticConfiguratorProvider p(binding(api));
	p.setAllowMutations(true);
	p.selectEndpoint(endpoint(p));
	CHECK(p.canEdit());
	p.refresh();
	CHECK(p.values()->rowCount() == 0);
	api.finish(false, "QueueFull");
	CHECK(p.canEdit());
	CHECK(p.values()->rowCount() == 1);
	p.refresh();
	api.finish(false, "Timeout");
	CHECK(!p.canEdit());
	CHECK(p.values()->rowCount() == 0);
	unsigned resets = 0;
	QObject::connect(p.values(), &QAbstractItemModel::modelReset, [&] {
		++resets;
		if (resets < 5)
			api.invalidate();
	});
	p.setAllowMutations(false);
	CHECK(resets == 1); // Nested changes schedule another owner turn, never recursive resets.
	for (int i = 0; i < 8; ++i)
		QCoreApplication::processEvents();
	CHECK(resets == 5);
	CHECK(!p.connected());
	CHECK(api.calls == QStringList({"read", "read"}));
	for (const quint32 responseType : {quint32{0}, quint32{3}}) {
		Api serviceApi;
		auto b = binding(serviceApi, 3, true);
		StaticType empty;
		empty.id = 3;
		empty.kind = StaticTypeKind::Struct;
		b.types.append(empty);
		b.endpoints[0].responseType = responseType;
		StaticConfiguratorProvider service(std::move(b));
		service.setAllowMutations(true);
		service.selectEndpoint(endpoint(service));
		CHECK(service.canCallService());
		service.callService();
		serviceApi.finish(true, {}, false,
		                  responseType == 0 ? QVariant{} : QVariant{QVariantMap{}});
		CHECK(service.responses()->rowCount() == 0);
		CHECK(service.serviceResponseEndpoint().isEmpty() == (responseType == 0));
		CHECK(service.serviceOutcome() ==
		      (responseType == 0 ? "Completed; no response value" : "Completed; value available"));
	}
}

static void staleCacheAfterWriteRefusal()
{
	Api api;
	StaticConfiguratorProvider p(binding(api));
	p.setAllowMutations(true);
	p.selectEndpoint(endpoint(p));
	p.beginEdit();
	CHECK(p.editing());
	p.refresh();
	CHECK(p.busy());
	api.finish(false, "Endpoint");
	CHECK(p.editing());
	CHECK(p.canApply());
	p.apply();
	CHECK(p.busy());
	api.finish(false, "QueueFull");
	CHECK(p.editing());
	CHECK(p.canApply());
	p.cancelEdit();
	CHECK(!p.canEdit());
	CHECK(p.values()->rowCount() == 0);
	CHECK(api.calls == QStringList({"read", "write"}));
}

template<int Index>
static QVariant watchCached(QObject* object)
{
	return static_cast<Api*>(object)->watched[Index];
}

template<int Index>
static QString watchRead(QObject* object)
{
	return static_cast<Api*>(object)->issue(QStringLiteral("watch%1").arg(Index));
}

template<std::size_t... Indices>
static constexpr auto watchReaders(std::index_sequence<Indices...>)
{
	return std::array{&watchRead<static_cast<int>(Indices)>...};
}

static StaticConfiguratorBinding watchBinding(Api& api, int count = 3)
{
	auto b = binding(api);
	StaticType u64;
	u64.id = 3;
	u64.kind = StaticTypeKind::Scalar;
	u64.scalarCode = 8;
	b.types.push_back(u64);
	StaticType s64;
	s64.id = 4;
	s64.kind = StaticTypeKind::Scalar;
	s64.scalarCode = 9;
	b.types.push_back(s64);
	StaticType enumeration;
	enumeration.id = 5;
	enumeration.kind = StaticTypeKind::Enum;
	enumeration.underlyingTypeId = 4;
	enumeration.entries.push_back({QStringLiteral("Known"), QStringLiteral("1")});
	b.types.push_back(enumeration);
	const auto prototype = b.endpoints.first();
	static constexpr auto readers = watchReaders(std::make_index_sequence<65>{});
	b.endpoints.clear();
	for (int i = 0; i < count; ++i) {
		auto e = prototype;
		e.id = static_cast<quint32>(i);
		e.name = QStringLiteral("Field%1").arg(i);
		e.valueType = i == 0 ? 3 : i == 1 ? 4 : i == 2 ? 5 : 1;
		if (i == 0) {
			e.cached = watchCached<0>;
		}
		if (i == 1) {
			e.cached = watchCached<1>;
		}
		if (i == 2) {
			e.cached = watchCached<2>;
		}
		if (i < static_cast<int>(readers.size()))
			e.read = readers[static_cast<std::size_t>(i)];
		b.endpoints.push_back(e);
	}
	return b;
}

static void watchEvents()
{
	QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	QCoreApplication::processEvents();
}

static void watchTick(StaticConfiguratorProvider& p)
{
	auto* timer = p.findChild<QTimer*>(QStringLiteral("staticConfiguratorWatchTimer"));
	CHECK(timer != nullptr);
	CHECK(timer && QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
}

static void staticTrend()
{
	Api api;
	api.watched[0] = QStringLiteral("42");
	StaticConfiguratorProvider p(watchBinding(api, 1));
	p.selectEndpoint(endpoint(p, 0));
	CHECK(p.addWatch());
	TrendHistory trend;
	trend.setSource(p.watchlist());
	trend.setRow(0);
	for (int i = 0; i < 2; ++i) {
		p.refreshWatch();
		CHECK(p.watchPending());
		api.finish(true);
		watchEvents();
	}
	CHECK(trend.points().size() == 2);
	CHECK(!trend.points().last().toMap().value("breakBefore").toBool());
	p.refreshWatch();
	api.finish(false, "Dispatch", false, {}, 9);
	watchEvents();
	p.refreshWatch();
	api.finish(true);
	watchEvents();
	CHECK(trend.points().size() == 3);
	CHECK(trend.points().last().toMap().value("breakBefore").toBool());
}

static int roleOf(const QAbstractItemModel* model, const QByteArray& name)
{
	return model ? model->roleNames().key(name, -1) : -1;
}

static void readWatched(StaticConfiguratorProvider& p, Api& api)
{
	p.refreshWatch();
	for (int guard = 0; p.watchPending() && guard < 16; ++guard) {
		api.finish(true);
		watchEvents();
	}
	CHECK(!p.watchPending());
}

// Adding, group-adding and removing Watchlist rows inserts or removes model rows,
// so a Trend keeps following its Field (the Static model used to reset itself on
// every edit and the Trend lost its selection: review probe of 2026-11-11).
static void staticTrendFollowsEdits()
{
	Api api;
	api.watched[0] = QStringLiteral("42");
	api.watched[1] = QStringLiteral("7");
	StaticConfiguratorProvider p(watchBinding(api, 3));
	auto* model = p.watchlist();
	p.selectEndpoint(endpoint(p, 1));
	CHECK(p.addWatch());
	TrendHistory trend;
	trend.setSource(model);
	trend.setRow(0);
	readWatched(p, api);
	CHECK(trend.row() == 0 && trend.points().size() == 1);

	p.selectEndpoint(endpoint(p, 0));
	CHECK(p.addWatch());
	CHECK(trend.row() == 0 && model->rowCount() == 2);
	readWatched(p, api);
	CHECK(trend.points().size() == 2);
	CHECK(!trend.points().last().toMap().value("breakBefore").toBool());

	const auto bulk = p.addWatchMany({QVariant::fromValue(endpoint(p, 2))});
	CHECK(bulk.value("added").toInt() == 1 && model->rowCount() == 3);
	CHECK(trend.row() == 0);
	readWatched(p, api);
	CHECK(trend.points().size() == 3);

	p.removeWatch(1);
	CHECK(trend.row() == 0 && model->rowCount() == 2);
	readWatched(p, api);
	CHECK(trend.points().size() == 4);
	CHECK(trend.points().last().toMap().value("value").toDouble() == 7.0);

	// Removing a row above the selection moves the selection with its Field.
	trend.setRow(1);
	readWatched(p, api);
	const auto before = trend.points().size();
	CHECK(before == 1 && trend.points().last().toMap().value("value").toDouble() == -77.0);
	p.removeWatch(0);
	CHECK(trend.row() == 0 && model->rowCount() == 1);
	readWatched(p, api);
	CHECK(trend.points().size() == before + 1);

	p.clearWatch();
	CHECK(trend.row() == -1 && model->rowCount() == 0);
}

// The number a Trend plots comes from the typed value: U64/S64 beyond 2^53 - 1
// are reported as not exact and carry no value; enums plot their code.
static void staticSampleRoles()
{
	Api api;
	StaticConfiguratorProvider p(watchBinding(api, 4));
	auto* model = p.watchlist();
	QVariantList all;
	for (int i = 0; i < 4; ++i)
		all.push_back(QVariant::fromValue(endpoint(p, i)));
	CHECK(p.addWatchMany(all).value("added").toInt() == 4);
	const int value = roleOf(model, "sampleValue"), exact = roleOf(model, "sampleExact"),
	          code = roleOf(model, "statusCode");
	CHECK(value >= 0 && exact >= 0 && code >= 0);
	CHECK(model->data(model->index(0, 0), code).toInt() == int(WatchStatus::NotRead));
	readWatched(p, api);
	for (int row = 0; row < 4; ++row)
		CHECK(model->data(model->index(row, 0), code).toInt() == int(WatchStatus::Ok));
	CHECK(!model->data(model->index(0, 0), exact).toBool());
	CHECK(!model->data(model->index(0, 0), value).isValid());
	CHECK(!model->data(model->index(1, 0), exact).toBool());
	CHECK(model->data(model->index(2, 0), exact).toBool());
	CHECK(model->data(model->index(2, 0), value).toDouble() == -77.0);
	CHECK(model->data(model->index(3, 0), exact).toBool());
	CHECK(model->data(model->index(3, 0), value).toDouble() == 17.0);
}

// Interval 0 reads continuously: the next cycle follows the last without the
// timer, still one read at a time.
static void watchContinuous()
{
	Api api;
	StaticConfiguratorProvider p(watchBinding(api));
	p.setWatchIntervalMs(0);
	CHECK(p.watchIntervalMs() == 0);
	for (int i = 0; i < 2; ++i) {
		p.selectEndpoint(endpoint(p, i));
		CHECK(p.addWatch());
	}
	auto* timer = p.findChild<QTimer*>(QStringLiteral("staticConfiguratorWatchTimer"));
	CHECK(timer && timer->interval() == 10);
	p.startWatch();
	CHECK(api.calls == QStringList{"watch0"});
	api.finish(true);
	watchEvents();
	CHECK(api.calls == QStringList({"watch0", "watch1"}));
	api.finish(true);
	watchEvents();
	watchEvents();
	// The cycle ended: the panel selects between cycles, and the next cycle
	// follows on the short timer.
	CHECK(api.calls.size() == 2 && !p.busy());
	p.selectEndpoint(endpoint(p, 2));
	CHECK(p.selection().endsWith("Field2"));
	watchTick(p);
	CHECK(api.calls == QStringList({"watch0", "watch1", "watch0"}));
	p.stopWatch();
	api.finish(true);
	watchEvents();
	CHECK(!p.watchActive() && api.calls.size() == 3);
}

static void watchLifecycle()
{
	Api api;
	StaticConfiguratorProvider p(watchBinding(api));
	CHECK(!p.watchReadManySupported() && p.watchIndividual());
	CHECK(!p.watchActive() && !p.watchPending() && p.watchCount() == 0);
	p.setWatchIntervalMs(60000);
	CHECK(p.watchIntervalMs() == 60000);
	p.setWatchIntervalMs(1);
	CHECK(p.watchIntervalMs() == 1);
	p.setWatchIntervalMs(0); // continuous
	CHECK(p.watchIntervalMs() == 0);
	p.setWatchIntervalMs(60001);
	p.setWatchIntervalMs(-1);
	CHECK(p.watchIntervalMs() == 0);
	p.setWatchIntervalMs(99);
	CHECK(p.watchIntervalMs() == 99);
	p.setWatchIntervalMs(60000);
	CHECK(p.watchIntervalMs() == 60000);
	for (int i = 0; i < 3; ++i) {
		p.selectEndpoint(endpoint(p, i));
		CHECK(p.canAddWatch() && p.addWatch());
		CHECK(!p.canAddWatch() && !p.addWatch());
	}
	CHECK(p.watchCount() == 3 && api.calls.isEmpty());
	auto* model = p.watchlist();
	const auto textRole = role(model, "text"), statusRole = role(model, "status");
	const auto sampleTextRole = role(model, "sampleText"), sampleRevisionRole = role(model, "sampleRevision");
	TrendHistory history;
	history.setSource(model);
	history.setRow(2);
	p.refreshWatch();
	CHECK(p.busy() && p.watchActive() && p.watchPending());
	CHECK(api.calls == QStringList{"watch0"});
	CHECK(model->data(model->index(0, 0), statusRole) == QStringLiteral("Pending"));
	CHECK(model->data(model->index(0, 0), textRole).toString().isEmpty());
	p.clearWatch();
	p.removeWatch(0);
	p.setWatchIntervalMs(100);
	CHECK(p.watchCount() == 3 && p.watchIntervalMs() == 60000);
	for (int tick = 0; tick < 5; ++tick)
		watchTick(p);
	CHECK(api.calls.size() == 1); // No pending request accumulation.
	api.finish(true);
	CHECK(!p.busy() && p.watchActive()); // Stable cycle before the next owner turn.
	p.selectEndpoint(endpoint(p, 0));
	CHECK(p.selection().endsWith("Field2"));
	watchEvents();
	CHECK(api.calls == QStringList({"watch0", "watch1"}) && p.watchPending());
	CHECK(model->data(model->index(0, 0), textRole).toString() == "18446744073709551615");
	api.finish(false, "Dispatch", false, {}, 9);
	watchEvents();
	CHECK(api.calls == QStringList({"watch0", "watch1", "watch2"}));
	CHECK(model->data(model->index(1, 0), statusRole) == QStringLiteral("Unavailable"));
	CHECK(model->data(model->index(1, 0), textRole).toString().isEmpty());
	api.finish(true);
	watchEvents();
	CHECK(!p.watchActive() && !p.busy());
	CHECK(model->data(model->index(2, 0), textRole).toString() == "-77");
	CHECK(history.points().size() == 1);
	CHECK(model->data(model->index(0, 0), sampleTextRole).toString() == "18446744073709551615");
	const auto sampleRevision = model->data(model->index(0, 0), sampleRevisionRole).toString();
	api.watched[0] = QStringLiteral("9007199254740993");
	api.changed();
	CHECK(model->data(model->index(0, 0), textRole).toString() == "9007199254740993");
	CHECK(model->data(model->index(0, 0), sampleTextRole).toString() == "18446744073709551615");
	CHECK(model->data(model->index(0, 0), sampleRevisionRole).toString() == sampleRevision);
	p.startWatch();
	CHECK(p.watchActive() && p.watchPending());
	p.stopWatch();
	CHECK(p.watchActive() && p.watchPending()); // Stop drains, never cancels host work.
	p.clearWatch();
	CHECK(p.watchCount() == 3);
	const auto issued = api.issued;
	api.finish(true);
	watchEvents();
	CHECK(!p.watchActive() && api.issued == issued);
	CHECK(p.watchStatus() == QStringLiteral("Stopped"));
	const auto old = model->index(0, 0);
	p.removeWatch(0);
	CHECK(p.watchCount() == 2 && !model->data(old, textRole).isValid());
	p.refreshWatch();
	CHECK(api.calls.last() == "watch1");
	api.invalidate();
	CHECK(model->data(model->index(0, 0), textRole).toString().isEmpty());
	CHECK(model->data(model->index(0, 0), sampleTextRole).toString().isEmpty());
	CHECK(model->data(model->index(0, 0), statusRole) == QStringLiteral("Stale"));
	api.ready = api.valid = true;
	++api.epoch;
	api.changed();
	api.finish(true);
	watchEvents();
	CHECK(!p.busy() && !p.watchActive());
	CHECK(model->data(model->index(0, 0), textRole).toString().isEmpty());
	CHECK(api.issued == issued + 1); // Admission never restarts the old cycle.
	p.refreshWatch();
	api.finish(true);
	watchEvents();
	CHECK(model->data(model->index(0, 0), textRole).toString() == "-9223372036854775808");
	api.finish(true);
	watchEvents();
	CHECK(!p.watchActive());
	p.clearWatch();
	CHECK(p.watchCount() == 0);
	p.setWatchIndividual(false);
	CHECK(p.watchIndividual() && p.watchStatus().contains("unavailable"));
}

static void watchBoundsAndReentrancy()
{
	Api api;
	StaticConfiguratorProvider p(watchBinding(api, 65));
	for (int i = 0; i < 65; ++i) {
		p.selectEndpoint(endpoint(p, i));
		CHECK(p.addWatch());
	}
	CHECK(!p.canAddWatch() && !p.addWatch() && p.watchCount() == 65);
	CHECK(api.calls.isEmpty());
	p.clearWatch();
	p.selectEndpoint(endpoint(p, 0));
	CHECK(p.addWatch());
	unsigned reentrant = 0;
	QObject::connect(p.watchlist(), &QAbstractItemModel::dataChanged, &p,
	                 [&](const QModelIndex&, const QModelIndex&, const QList<int>&) {
		                 ++reentrant;
		                 p.clearWatch();
		                 p.removeWatch(0);
		                 p.startWatch();
		                 p.refreshWatch();
		                 CHECK(!p.addWatch());
	                 });
	p.startWatch();
	CHECK(reentrant > 0 && p.watchCount() == 1 && api.calls.size() == 1);
	api.finish(true);
	watchEvents();
	CHECK(p.watchCount() == 1 && api.calls.size() == 1);
	// Polling is paused behind a foreground operation, rather than queued.
	p.refresh();
	CHECK(p.busy() && api.calls.last() == "watch0");
	const auto beforeTicks = api.issued;
	for (int i = 0; i < 3; ++i)
		watchTick(p);
	CHECK(api.issued == beforeTicks);
	api.finish(true);
	watchEvents();
	watchTick(p);
	CHECK(api.issued == beforeTicks + 1 && p.watchPending());
	p.stopWatch();
	api.finish(false, "Timeout");
	watchEvents();
	CHECK(!p.watchActive() && p.watchlist()
	                              ->data(p.watchlist()->index(0, 0), role(p.watchlist(), "text"))
	                              .toString()
	                              .isEmpty());
	// Models can request deferred destruction from structural callbacks.
	Api other;
	QPointer<StaticConfiguratorProvider> doomed =
	    new StaticConfiguratorProvider(watchBinding(other));
	doomed->selectEndpoint(endpoint(*doomed));
	QObject::connect(doomed->watchlist(), &QAbstractItemModel::rowsAboutToBeInserted, doomed,
	                 [doomed] {
		                 doomed->deleteLater();
	                 });
	CHECK(doomed->addWatch());
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	CHECK(!doomed);
	other.changed();
}

static void bulkWatch()
{
	Api api, other;
	StaticConfiguratorProvider p(watchBinding(api, 65)), foreign(watchBinding(other));
	p.setAllowMutations(true);
	p.selectEndpoint(endpoint(p, 3));
	p.beginEdit();
	const auto oldSelection = p.selectedEndpointIndex();
	const auto value = p.values()->index(0, 0);
	CHECK(p.editing() && p.setScalarText(value, "42"));
	QVariantList indices;
	for (int i = 0; i < 65; ++i) {
		CHECK(p.watchCandidate(endpoint(p, i)));
		indices.push_back(QVariant::fromValue(endpoint(p, i)));
	}
	indices.push_back(QVariant::fromValue(endpoint(p, 0)));
	int resets = 0, inserts = 0;
	QObject::connect(p.watchlist(), &QAbstractItemModel::modelReset, &p, [&] { ++resets; });
	QObject::connect(p.watchlist(), &QAbstractItemModel::rowsInserted, &p, [&] { ++inserts; });
	const auto bad = p.addWatchMany({indices[0], QVariant::fromValue(endpoint(foreign))});
	CHECK(bad.value("status") == "invalid" && p.watchCount() == 0 && inserts == 0);
	const auto result = p.addWatchMany(indices);
	CHECK(result.value("status") == "ok" && result.value("added").toInt() == 65);
	// One group is one structural publication, and it inserts rather than resets.
	CHECK(result.value("duplicate").toInt() == 1 && inserts == 1 && resets == 0 &&
	      api.calls.isEmpty());
	CHECK(p.selectedEndpointIndex() == oldSelection && p.editing());
	CHECK(p.values()->data(value, role(p.values(), "editorText")).toString() == "42");
	p.refreshWatch();
	CHECK(api.calls.isEmpty()); // A draft prevents a polling cycle on this provider.
	p.cancelEdit();
	api.invalidate();
	CHECK(p.watchCandidate(endpoint(p, 0)));
	CHECK(p.addWatchMany(indices).value("status") == "refused");
	CHECK(p.watchCount() == 65);

	Api changing;
	StaticConfiguratorProvider live(watchBinding(changing));
	QObject::connect(live.watchlist(), &QAbstractItemModel::rowsAboutToBeInserted, &live,
	                 [&] { changing.invalidate(); });
	const auto committed = live.addWatchMany({QVariant::fromValue(endpoint(live))});
	CHECK(committed.value("added").toInt() == 1);
	watchEvents();
	CHECK(!live.connected() && live.watchCount() == 1 && changing.calls.isEmpty());
	CHECK(live.watchlist()->data(live.watchlist()->index(0, 0),
	                             role(live.watchlist(), "status")).toString() == "Stale");
	Api sessionChange;
	auto mixed = watchBinding(sessionChange);
	auto service = binding(sessionChange, 3, true).endpoints.first();
	service.name = "Service";
	mixed.endpoints.push_back(service);
	StaticConfiguratorProvider sessionProvider(std::move(mixed));
	sessionProvider.setAllowMutations(true);
	auto* tree = sessionProvider.endpoints();
	sessionProvider.selectEndpoint(tree->index(0, 0, tree->index(1, 0)));
	sessionProvider.callService();
	sessionChange.finish(true, {}, false, quint32{42});
	auto* response = sessionProvider.responses();
	const auto responseIndex = response->index(0, 0);
	CHECK(response->data(responseIndex, role(response, "value")).toString() == "42");
	sessionProvider.selectEndpoint(endpoint(sessionProvider));
	sessionProvider.beginEdit();
	auto* values = sessionProvider.values();
	const auto draftIndex = values->index(0, 0);
	CHECK(sessionProvider.editing());
	CHECK(!values->data(draftIndex, role(values, "editorText")).toString().isEmpty());
	bool hiddenSynchronously = false;
	QObject::connect(sessionProvider.watchlist(), &QAbstractItemModel::rowsAboutToBeInserted,
	                 &sessionProvider, [&] {
		                 ++sessionChange.epoch; // The transport remains ready.
		                 sessionChange.changed();
		                 hiddenSynchronously =
		                     values->data(draftIndex, role(values, "editorText")).toString().isEmpty() &&
		                     response->data(responseIndex, role(response, "value")).toString().isEmpty();
	                 });
	const auto sessionResult = sessionProvider.addWatchMany(
	    {QVariant::fromValue(endpoint(sessionProvider))});
	CHECK(sessionResult.value("added").toInt() == 1 && hiddenSynchronously);
	watchEvents();
	Api destroyed;
	QObject observer;
	QPointer<StaticConfiguratorProvider> doomed = new StaticConfiguratorProvider(watchBinding(destroyed));
	// Qt still touches a model after emitting rowsAboutToBeInserted; rowsInserted
	// is emitted last, so synchronous deletion is exercised from there.
	QObject::connect(doomed->watchlist(), &QAbstractItemModel::rowsInserted, &observer,
	                 [&] { delete doomed.data(); });
	const auto sourceIndex = endpoint(*doomed);
	const auto destruction = doomed->addWatchMany({QVariant::fromValue(sourceIndex)});
	CHECK(!doomed && destruction.value("added").toInt() == 1 && destroyed.calls.isEmpty());
}

// The Device tree's watch boxes (T-1), with the Dynamic provider's contracts: a
// group's box covers its Fields and shows all, some or none; one change and no
// request; Undo puts rows back in their places and goes once anything else
// changes the Watchlist; a group that does not fit changes nothing.
static void groupWatchBoxes()
{
	Api api, other;
	StaticConfiguratorProvider p(watchBinding(api, 65)), foreign(watchBinding(other));
	const auto group = p.endpoints()->index(0, 0);
	const auto watched = [](const QModelIndex& index) { return QVariantList{QVariant::fromValue(index)}; };
	CHECK(p.watchState(group) == Qt::Unchecked && p.watchState(endpoint(p, 0)) == Qt::Unchecked &&
	      !p.canUndoWatch() && p.watchLimit() == 1024);
	auto result = p.setWatchMany(watched(endpoint(p, 0)), true);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 1);
	CHECK(p.watchState(endpoint(p, 0)) == Qt::Checked && p.watchState(group) == Qt::PartiallyChecked);
	int inserts = 0;
	QObject::connect(p.watchlist(), &QAbstractItemModel::rowsInserted, &p, [&] { ++inserts; });
	result = p.setWatchMany(watched(group), true);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 64 &&
	      result.value("unchanged").toInt() == 1 && result.value("fields").toInt() == 65);
	CHECK(inserts == 1 && p.watchCount() == 65 && p.watchState(group) == Qt::Checked && p.canUndoWatch());
	result = p.undoWatch();
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 64);
	CHECK(p.watchCount() == 1 && p.watchState(group) == Qt::PartiallyChecked && !p.canUndoWatch());
	// Unwatching the group, then Undo: the rows come back in their order.
	CHECK(p.setWatchMany(watched(endpoint(p, 2)), true).value("changed").toInt() == 1);
	CHECK(p.setWatchMany(watched(endpoint(p, 1)), true).value("changed").toInt() == 1);
	const auto names = [&] {
		QStringList list;
		for (int row = 0; row < p.watchlist()->rowCount(); ++row)
			list << p.watchlist()->index(row, 0).data(Qt::DisplayRole).toString();
		return list;
	};
	const auto before = names();
	CHECK(before.size() == 3);
	result = p.setWatchMany(watched(group), false);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 3 && p.watchCount() == 0 &&
	      p.watchState(group) == Qt::Unchecked);
	CHECK(p.undoWatch().value("changed").toInt() == 3 && names() == before);
	// Rows apart from one another: taken out run by run, put back in place,
	// fresh and not read yet.
	CHECK(p.setWatchMany(watched(endpoint(p, 3)), true).value("changed").toInt() == 1);
	const auto four = names();
	result = p.setWatchMany({QVariant::fromValue(endpoint(p, 0)), QVariant::fromValue(endpoint(p, 1))}, false);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 2 && p.watchCount() == 2);
	CHECK(p.undoWatch().value("changed").toInt() == 2 && names() == four);
	CHECK(p.watchlist()->index(0, 0).data(role(p.watchlist(), "status")).toString() == "NotRead");
	CHECK(p.setWatchMany(watched(endpoint(p, 3)), false).value("changed").toInt() == 1);
	// Another change of the Watchlist takes Undo away.
	CHECK(p.setWatchMany(watched(group), true).value("status") == "ok" && p.canUndoWatch());
	p.removeWatch(0);
	CHECK(!p.canUndoWatch() && p.undoWatch().value("status") == "refused");
	// Another provider's index is refused; nothing reached the device.
	CHECK(p.setWatchMany(watched(endpoint(foreign, 0)), true).value("status") == "invalid");
	CHECK(api.calls.isEmpty());

	// More Fields than the Watchlist holds: nothing changes, and room says why.
	Api wide;
	StaticConfiguratorProvider large(watchBinding(wide, 1030));
	const auto all = large.endpoints()->index(0, 0);
	result = large.setWatchMany(watched(all), true);
	CHECK(result.value("status") == "capacity" && result.value("fields").toInt() == 1030 &&
	      result.value("room").toInt() == 1024 && large.watchCount() == 0 &&
	      large.watchState(all) == Qt::Unchecked && !large.canUndoWatch());
}

static void largeWatchCycle()
{
	Api api;
	StaticConfiguratorProvider p(watchBinding(api, 65));
	for (int i = 0; i < 65; ++i) {
		p.selectEndpoint(endpoint(p, i));
		CHECK(p.addWatch());
	}
	p.setWatchIntervalMs(1);
	auto* timer = p.findChild<QTimer*>(QStringLiteral("staticConfiguratorWatchTimer"));
	CHECK(timer && timer->interval() == 1 && timer->timerType() == Qt::PreciseTimer);
	CHECK(p.watchCount() == 65 && api.calls.isEmpty());
	p.startWatch();
	for (int row = 0; row < 65; ++row) {
		CHECK(p.watchPending() && p.watchActive());
		CHECK(api.issued == static_cast<unsigned>(row + 1));
		CHECK(api.calls.last() == QStringLiteral("watch%1").arg(row));
		for (int tick = 0; tick < 3; ++tick)
			watchTick(p);
		CHECK(api.issued == static_cast<unsigned>(row + 1));
		api.finish(true);
		CHECK(!p.watchPending() && p.watchActive());
		if (row == 64)
			p.stopWatch(); // Stop before the final queued continuation can start another cycle.
		watchEvents();
	}
	CHECK(!p.watchActive() && !p.busy() && api.issued == 65);
	CHECK(p.watchCount() == 65);
	for (int row = 0; row < 65; ++row)
		CHECK(p.watchlist()->data(p.watchlist()->index(row, 0), role(p.watchlist(), "status")) ==
		      QStringLiteral("Ok"));
	p.startWatch();
	CHECK(p.watchPending() && api.issued == 66);
	p.stopWatch();
	p.clearWatch();
	CHECK(p.watchCount() == 65 && p.watchActive());
	api.finish(true);
	watchEvents();
	CHECK(api.issued == 66 && !p.watchActive());
	p.clearWatch();
	CHECK(p.watchCount() == 0);
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	actions();
	selectionIdentity();
	draftAndReadback();
	sessionPermission();
	responseMutationLifetime();
	paging();
	reentrantInsert();
	destruction();
	budgetsAndForeignIndices();
	readRefusalsAndQueuedReset();
	staleCacheAfterWriteRefusal();
	watchLifecycle();
	watchContinuous();
	staticTrend();
	staticTrendFollowsEdits();
	staticSampleRoles();
	watchBoundsAndReentrancy();
	bulkWatch();
	groupWatchBoxes();
	largeWatchCycle();
	std::printf("{\"passed\":%s,\"checks\":%u}\n", failures ? "false" : "true", checks);
	return failures ? 1 : 0;
}
