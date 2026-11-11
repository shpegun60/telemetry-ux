/* Bounded trusted-localhost asset map and existing native Encoded lease. MIT. */
#include "WebServer.hpp"
#include "DeviceServer.hpp"
#include "Device.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkRequest>
#include <QWebSocket>
#include <QWebSocketCorsAuthenticator>
#include <QWebSocketServer>
#include <algorithm>
#include <cmath>

namespace {
constexpr qsizetype assetBudget = 4 * 1024 * 1024;
constexpr qsizetype headerBudget = 8192;
constexpr qsizetype frameBudget = 8192;
constexpr int maxHttpConnections = 16;
constexpr auto protocol = "telemetry.encoded.v1";

struct Asset {
	QByteArray bytes, mime;
};

bool plainPath(const QString& value)
{
	if (!value.startsWith('/') || value.endsWith('/') || value.size() > 1024)
		return false;
	static const QRegularExpression segment{QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]*$")};
	const auto pieces = value.mid(1).split('/');
	return !pieces.isEmpty() && std::all_of(pieces.begin(), pieces.end(), [](const QString& part) {
		return part != QStringLiteral(".") && part != QStringLiteral("..") && !part.endsWith('.') &&
		       segment.match(part).hasMatch();
	});
}

QByteArray expectedMime(const QString& path)
{
	const auto suffix = QFileInfo(path).suffix().toLower();
	static const QHash<QString, QByteArray> types{{"html", "text/html; charset=utf-8"},
	                                              {"js", "text/javascript; charset=utf-8"},
	                                              {"mjs", "text/javascript; charset=utf-8"},
	                                              {"css", "text/css; charset=utf-8"},
	                                              {"json", "application/json"},
	                                              {"bin", "application/octet-stream"},
	                                              {"svg", "image/svg+xml"},
	                                              {"png", "image/png"},
	                                              {"ico", "image/x-icon"},
	                                              {"woff2", "font/woff2"}};
	return QFileInfo(path).fileName() == QStringLiteral("LICENSE")
	           ? QByteArray{"text/plain; charset=utf-8"}
	           : types.value(suffix);
}

bool safeFilesystemPath(const QString& absolute)
{
	QFileInfo info(absolute);
	while (true) {
		if (!info.exists() || info.isSymLink() || info.isJunction())
			return false;
		const auto parent = info.absolutePath();
		if (parent == info.absoluteFilePath())
			break;
		info.setFile(parent);
	}
	return true;
}

QByteArray descriptorBytes()
{
	const auto bytes = emulator::descriptor();
	return {reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())};
}

bool parseHeaders(const QByteArray& request, QList<QByteArray>& first,
                  QHash<QByteArray, QByteArray>& headers)
{
	if (!request.endsWith("\r\n\r\n"))
		return false;
	auto stripped = request;
	stripped.replace("\r\n", "");
	if (stripped.contains('\r') || stripped.contains('\n'))
		return false;
	const auto lines = request.left(request.size() - 4).split('\n');
	if (lines.isEmpty())
		return false;
	first = lines.first().trimmed().split(' ');
	if (first.size() != 3 || first[2] != "HTTP/1.1")
		return false;
	for (int i = 1; i < lines.size(); ++i) {
		auto line = lines[i];
		if (line.endsWith('\r'))
			line.chop(1);
		const auto colon = line.indexOf(':');
		if (colon <= 0 || line.startsWith(' ') || line.startsWith('\t'))
			return false;
		const auto name = line.left(colon).toLower();
		if (std::any_of(name.begin(), name.end(),
		                [](char c) {
			                return !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-';
		                }) ||
		    headers.contains(name))
			return false;
		const auto value = line.mid(colon + 1).trimmed();
		if (std::any_of(value.begin(), value.end(), [](char c) {
			    return static_cast<unsigned char>(c) < 32 || c == 127;
		    }))
			return false;
		headers.insert(name, value);
	}
	return true;
}
} // namespace

struct WebServer::Impl {
	WebServer& owner;
	DeviceServer& device;
	QTcpServer http;
	QTcpServer wsIngress;
	QWebSocketServer websocket{QStringLiteral("Telemetry localhost"),
	                           QWebSocketServer::NonSecureMode};
	QPointer<QWebSocket> socket;
	QHash<QTcpSocket*, QByteArray> incoming;
	QSet<QTcpSocket*> httpClients;
	QPointer<QTcpSocket> pendingUpgrade;
	QHash<QString, Asset> assets;
	QString error, origin;
	quint32 session = 0;
	bool handling = false, closing = false;

	Impl(WebServer& parent, DeviceServer& native) : owner(parent), device(native)
	{
		http.setMaxPendingConnections(maxHttpConnections);
		wsIngress.setMaxPendingConnections(1);
		websocket.setMaxPendingConnections(1);
		websocket.setHandshakeTimeout(5000);
		websocket.setSupportedSubprotocols({QString::fromLatin1(protocol)});
		QObject::connect(&http, &QTcpServer::newConnection, &owner, [this] {
			acceptHttp();
		});
		QObject::connect(&wsIngress, &QTcpServer::newConnection, &owner, [this] {
			acceptUpgrade();
		});
		QObject::connect(&websocket, &QWebSocketServer::originAuthenticationRequired, &owner,
		                 [this](QWebSocketCorsAuthenticator* request) {
			                 request->setAllowed(!closing && !origin.isEmpty() &&
			                                     request->origin() == origin);
		                 });
		QObject::connect(&websocket, &QWebSocketServer::newConnection, &owner, [this] {
			acceptWebSocket();
		});
		QObject::connect(&device, &DeviceServer::webLeaseClosed, &owner, [this] {
			session = 0;
			if (socket)
				socket->close(QWebSocketProtocol::CloseCodeGoingAway,
				              QStringLiteral("Native session closed"));
		});
	}

	bool load(const QString& root)
	{
		const QDir directory(root);
		if (root.isEmpty() || !safeFilesystemPath(directory.absolutePath())) {
			error = QStringLiteral("Web root must be a stable directory without links");
			return false;
		}
		const auto manifestPath = directory.filePath(QStringLiteral("assets.json"));
		const QFileInfo manifestInfo(manifestPath);
		if (!safeFilesystemPath(manifestPath) || !manifestInfo.isFile() ||
		    manifestInfo.size() > 262144) {
			error = QStringLiteral("Missing or oversized assets.json");
			return false;
		}
		QFile manifest(manifestPath);
		if (!manifest.open(QIODevice::ReadOnly)) {
			error = manifest.errorString();
			return false;
		}
		QJsonParseError parseError{};
		const auto document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
			error = QStringLiteral("Invalid assets manifest JSON");
			return false;
		}
		const auto object = document.object();
		const auto list = object.value(QStringLiteral("assets")).toArray();
		if (object.value(QStringLiteral("format")).toString() !=
		        QStringLiteral("telemetry-ux-web-assets-v1") ||
		    object.value(QStringLiteral("entry")).toString() != QStringLiteral("/index.html") ||
		    !object.value(QStringLiteral("packages")).isObject() || list.isEmpty() ||
		    list.size() > 256) {
			error = QStringLiteral("Invalid assets manifest contract");
			return false;
		}
		QHash<QString, Asset> candidate;
		qsizetype total = 0;
		QHash<QString, bool> folded;
		for (const auto& item : list) {
			if (!item.isObject()) {
				error = QStringLiteral("Invalid asset entry");
				return false;
			}
			const auto entry = item.toObject();
			const auto path = entry.value(QStringLiteral("path")).toString();
			const auto mime = entry.value(QStringLiteral("mime")).toString().toLatin1();
			const auto expected = entry.value(QStringLiteral("sha256")).toString().toLatin1();
			const auto length = entry.value(QStringLiteral("bytes")).toDouble(-1);
			if (!plainPath(path) || candidate.contains(path) ||
			    folded.contains(path.toCaseFolded()) || mime.isEmpty() ||
			    mime != expectedMime(path) || expected.size() != 64 ||
			    QByteArray::fromHex(expected).toHex() != expected || length < 0 ||
			    std::floor(length) != length || length > assetBudget - total ||
			    path == QStringLiteral("/assets.json")) {
				error = QStringLiteral("Invalid asset path, MIME, length or digest");
				return false;
			}
			const auto absolute = directory.filePath(path.mid(1));
			const QFileInfo info(absolute);
			if (!safeFilesystemPath(absolute) || !info.isFile() ||
			    info.size() != static_cast<qint64>(length)) {
				error = QStringLiteral("Asset file changed or has a link: %1").arg(path);
				return false;
			}
			QFile file(absolute);
			if (!file.open(QIODevice::ReadOnly)) {
				error = file.errorString();
				return false;
			}
			const auto bytes = file.read(assetBudget - total + 1);
			if (bytes.size() != static_cast<qsizetype>(length) || !file.atEnd() ||
			    QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != expected) {
				error = QStringLiteral("Asset digest mismatch: %1").arg(path);
				return false;
			}
			total += bytes.size();
			candidate.insert(path, {bytes, mime});
			folded.insert(path.toCaseFolded(), true);
		}
		if (!candidate.contains(QStringLiteral("/index.html")) ||
		    object.value(QStringLiteral("totalBytes")).toDouble(-1) != total) {
			error = QStringLiteral("Asset total or entry point mismatch");
			return false;
		}
		assets = std::move(candidate);
		return true;
	}

	void answer(QTcpSocket* client, int status, const QByteArray& bytes = {},
	            const QByteArray& mime = "text/plain; charset=utf-8")
	{
		if (!incoming.contains(client))
			return;
		incoming.remove(client);
		const auto reason = status == 200   ? "OK"
		                    : status == 404 ? "Not Found"
		                    : status == 405 ? "Method Not Allowed"
		                                    : "Bad Request";
		QByteArray header =
		    "HTTP/1.1 " + QByteArray::number(status) + " " + reason +
		    "\r\nContent-Length: " + QByteArray::number(bytes.size()) +
		    "\r\nContent-Type: " + mime +
		    "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n";
		if (client->write(header) != header.size() ||
		    (!bytes.isEmpty() && client->write(bytes) != bytes.size()))
			client->abort();
		else
			client->disconnectFromHost();
	}

	void acceptHttp()
	{
		while (http.hasPendingConnections()) {
			auto* client = http.nextPendingConnection();
			if (httpClients.size() >= maxHttpConnections) {
				client->abort();
				client->deleteLater();
				continue;
			}
			client->setReadBufferSize(headerBudget + 1);
			incoming.insert(client, {});
			httpClients.insert(client);
			QObject::connect(client, &QTcpSocket::readyRead, &owner, [this, client] {
				readHttp(client);
			});
			QObject::connect(client, &QTcpSocket::disconnected, &owner, [this, client] {
				incoming.remove(client);
				httpClients.remove(client);
				client->deleteLater();
			});
			QPointer<QTcpSocket> guarded = client;
			QTimer::singleShot(5000, &owner, [guarded] {
				if (guarded)
					guarded->abort();
			});
			if (client->bytesAvailable())
				readHttp(client);
		}
	}

	void readHttp(QTcpSocket* client)
	{
		auto it = incoming.find(client);
		if (it == incoming.end())
			return;
		if (it->size() + client->bytesAvailable() > headerBudget) {
			answer(client, 400);
			return;
		}
		it->append(client->readAll());
		const auto end = it->indexOf("\r\n\r\n");
		if (end < 0)
			return;
		if (end + 4 != it->size()) {
			answer(client, 400);
			return;
		}
		QList<QByteArray> first;
		QHash<QByteArray, QByteArray> headers;
		if (!parseHeaders(*it, first, headers)) {
			answer(client, 400);
			return;
		}
		if (first[0] != "GET") {
			answer(client, 405);
			return;
		}
		const auto rawPath = first[1];
		const auto path = QString::fromLatin1(rawPath);
		if ((path != QStringLiteral("/") && !plainPath(path)) || rawPath != path.toLatin1()) {
			answer(client, 400);
			return;
		}
		if (headers.value("host") !=
		        QByteArray{"127.0.0.1:"} + QByteArray::number(http.serverPort()) ||
		    (headers.contains("origin") && headers.value("origin") != origin.toLatin1()) ||
		    headers.contains("transfer-encoding") ||
		    (headers.contains("content-length") && headers.value("content-length") != "0")) {
			answer(client, 400);
			return;
		}
		if (path == QStringLiteral("/demo/transport")) {
			const QJsonObject value{
			    {"kind", "websocket"},
			    {"url", QStringLiteral("ws://127.0.0.1:%1/encoded").arg(wsIngress.serverPort())},
			    {"protocol", QString::fromLatin1(protocol)}};
			answer(client, 200, QJsonDocument(value).toJson(QJsonDocument::Compact),
			       "application/json");
		} else if (path == QStringLiteral("/descriptor.bin"))
			answer(client, 200, descriptorBytes(), "application/octet-stream");
		else if (path == QStringLiteral("/schema-header.bin"))
			answer(client, 200, descriptorBytes().first(64), "application/octet-stream");
		else if (path == QStringLiteral("/capabilities/read-many.bin") ||
		         path == QStringLiteral("/capabilities/batch.bin")) {
			const auto bytes = path.endsWith(QStringLiteral("/read-many.bin"))
			                       ? device.webReadManyCapability()
			                       : device.webBatchCapability();
			answer(client, bytes.isEmpty() ? 404 : 200, bytes, "application/octet-stream");
		} else {
			const auto asset = assets.constFind(
			    path == QStringLiteral("/") ? QStringLiteral("/index.html") : path);
			if (asset == assets.cend())
				answer(client, 404);
			else
				answer(client, 200, asset->bytes, asset->mime);
		}
	}

	void acceptUpgrade()
	{
		while (wsIngress.hasPendingConnections()) {
			auto* client = wsIngress.nextPendingConnection();
			if (pendingUpgrade || socket || session || device.connected()) {
				client->abort();
				client->deleteLater();
				continue;
			}
			pendingUpgrade = client;
			client->setReadBufferSize(headerBudget + 1);
			QObject::connect(client, &QTcpSocket::readyRead, &owner, [this, client] {
				readUpgrade(client);
			});
			QObject::connect(client, &QTcpSocket::disconnected, &owner, [this, client] {
				if (pendingUpgrade == client) {
					pendingUpgrade.clear();
					client->deleteLater();
				}
			});
			QPointer<QTcpSocket> guarded = client;
			QTimer::singleShot(5000, &owner, [this, guarded] {
				if (guarded && pendingUpgrade == guarded) {
					pendingUpgrade.clear();
					guarded->abort();
					guarded->deleteLater();
				}
			});
			if (client->bytesAvailable())
				readUpgrade(client);
		}
	}

	void readUpgrade(QTcpSocket* client)
	{
		if (pendingUpgrade != client)
			return;
		const auto bytes = client->peek(headerBudget + 1);
		const auto end = bytes.indexOf("\r\n\r\n");
		if (bytes.size() <= headerBudget && end < 0)
			return;
		QList<QByteArray> first;
		QHash<QByteArray, QByteArray> headers;
		if (bytes.size() > headerBudget || end + 4 != bytes.size() ||
		    !parseHeaders(bytes, first, headers) || first[0] != "GET" || first[1] != "/encoded" ||
		    headers.value("host") !=
		        QByteArray{"127.0.0.1:"} + QByteArray::number(wsIngress.serverPort()) ||
		    headers.value("origin") != origin.toLatin1() || headers.contains("transfer-encoding") ||
		    (headers.contains("content-length") && headers.value("content-length") != "0") ||
		    !headers.value("sec-websocket-protocol").split(',').contains(QByteArray{protocol})) {
			pendingUpgrade.clear();
			client->abort();
			client->deleteLater();
			return;
		}
		// Only bound the HTTP upgrade envelope. Qt owns all handshake/framing
		// parsing and consumes the original, still-unread socket bytes.
		QObject::disconnect(client, &QTcpSocket::readyRead, &owner, nullptr);
		websocket.handleConnection(client);
	}

	void acceptWebSocket()
	{
		while (websocket.hasPendingConnections()) {
			auto* candidate = websocket.nextPendingConnection();
			pendingUpgrade.clear();
			candidate->setReadBufferSize(frameBudget);
			candidate->setMaxAllowedIncomingFrameSize(frameBudget);
			candidate->setMaxAllowedIncomingMessageSize(frameBudget);
			if (socket || session || candidate->origin() != origin ||
			    candidate->subprotocol() != QString::fromLatin1(protocol) ||
			    candidate->requestUrl().path() != QStringLiteral("/encoded") ||
			    candidate->requestUrl().hasQuery() ||
			    candidate->request().rawHeader("Host") !=
			        QByteArray{"127.0.0.1:"} + QByteArray::number(wsIngress.serverPort())) {
				candidate->close(QWebSocketProtocol::CloseCodePolicyViolated,
				                 QStringLiteral("Unexpected WebSocket profile"));
				candidate->deleteLater();
				continue;
			}
			const auto claimed = device.claimWebLease();
			if (!claimed) {
				candidate->close(QWebSocketProtocol::CloseCodePolicyViolated,
				                 QStringLiteral("Native owner is busy"));
				candidate->deleteLater();
				continue;
			}
			socket = candidate;
			session = claimed;
			QObject::connect(candidate, &QWebSocket::disconnected, &owner, [this, candidate] {
				if (socket == candidate) {
					socket.clear();
					const auto old = session;
					session = 0;
					device.releaseWebLease(old);
				}
				candidate->deleteLater();
			});
			QObject::connect(
			    candidate, &QWebSocket::textMessageReceived, &owner, [candidate](const QString&) {
				    candidate->close(QWebSocketProtocol::CloseCodeDatatypeNotSupported,
				                     QStringLiteral("Binary Encoded request required"));
			    });
			QObject::connect(
			    candidate, &QWebSocket::binaryMessageReceived, &owner,
			    [this, candidate](const QByteArray& bytes) {
				    if (socket != candidate || !session || handling ||
				        candidate->bytesToWrite() != 0 || bytes.size() > frameBudget) {
					    candidate->close(QWebSocketProtocol::CloseCodePolicyViolated,
					                     QStringLiteral("One pending exchange required"));
					    return;
				    }
				    handling = true;
				    const auto reply = device.exchangeWebLease(session, bytes);
				    handling = false;
				    if (reply.isEmpty() || reply.size() > frameBudget || socket != candidate ||
				        !session || candidate->sendBinaryMessage(reply) != reply.size() ||
				        candidate->bytesToWrite() > frameBudget + 64)
					    candidate->close(QWebSocketProtocol::CloseCodePolicyViolated,
					                     QStringLiteral("Encoded exchange refused"));
			    });
			const QJsonObject greeting{{"session", static_cast<qint64>(session)}};
			candidate->sendTextMessage(
			    QString::fromUtf8(QJsonDocument(greeting).toJson(QJsonDocument::Compact)));
		}
	}

	void close()
	{
		closing = true;
		http.close();
		wsIngress.close();
		websocket.close();
		const auto clients = httpClients.values();
		incoming.clear();
		httpClients.clear();
		for (auto* client : clients)
			client->abort();
		if (pendingUpgrade) {
			pendingUpgrade->abort();
			pendingUpgrade->deleteLater();
			pendingUpgrade.clear();
		}
		if (socket) {
			auto* old = socket.data();
			socket.clear();
			old->abort();
			old->deleteLater();
		}
		const auto old = session;
		session = 0;
		device.releaseWebLease(old);
		assets.clear();
		origin.clear();
	}
};

WebServer::WebServer(DeviceServer& device, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>(*this, device))
{}

WebServer::~WebServer()
{
	close();
}

bool WebServer::listen(const QString& root, quint16 httpPort, quint16 wsPort)
{
	d->close();
	d->error.clear();
	if (!d->load(root))
		return false;
	if (!d->wsIngress.listen(QHostAddress::LocalHost, wsPort)) {
		d->error = d->wsIngress.errorString();
		d->close();
		return false;
	}
	if (!d->http.listen(QHostAddress::LocalHost, httpPort)) {
		d->error = d->http.errorString();
		d->close();
		return false;
	}
	d->origin = QStringLiteral("http://127.0.0.1:%1").arg(d->http.serverPort());
	d->closing = false;
	return true;
}

void WebServer::close()
{
	d->close();
}

QString WebServer::errorString() const
{
	return d->error;
}

QString WebServer::url() const
{
	return d->origin + QStringLiteral("/");
}

quint16 WebServer::httpPort() const
{
	return d->http.serverPort();
}
