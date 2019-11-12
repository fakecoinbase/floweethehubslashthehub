/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QStandardPaths>

#include <FloweeServiceApplication.h>
#include <WorkerThreads.h>
#include <httpengine/server.h>
#include <Message.h>

#include "BitcoreProxy.h"

#define PORT 3000

class Server : public HttpEngine::Server
{
public:
    Server(const std::function<void(HttpEngine::WebRequest*)> &handler)
        : m_func(handler)
    {
    }
    HttpEngine::WebRequest *createRequest(qintptr socketDescriptor) override {
        return new BitcoreWebRequest(socketDescriptor, m_func);
    }

    void setProxy(BitcoreProxy *proxy) { m_handler = proxy; }
private:
    BitcoreProxy *m_handler;
    std::function<void(HttpEngine::WebRequest*)> m_func;
};

Q_DECLARE_METATYPE(Message)

int main(int argc, char **argv)
{
    FloweeServiceApplication app(argc, argv);
    app.setOrganizationName("flowee");
    app.setOrganizationDomain("flowee.org");
    app.setApplicationName("bitcore-proxy");

    QCommandLineParser parser;
    parser.setApplicationDescription("BitCore proxy server");
    parser.addHelpOption();
    QCommandLineOption conf(QStringList() << "conf", "config file", "FILENAME");
    parser.addOption(conf);
    app.addServerOptions(parser);
    parser.process(app.arguments());

    app.setup("webserver.log", parser.value(conf));

    qRegisterMetaType<Message>();

    BitcoreProxy handler;
    Server server(std::bind(&BitcoreProxy::onIncomingConnection, &handler, std::placeholders::_1));
    server.setProxy(&handler);

    auto listenAddresses = app.bindingEndPoints(parser, PORT);
    if (listenAddresses.isEmpty()) {
        using boost::asio::ip::tcp;
        listenAddresses.push_back(tcp::endpoint(boost::asio::ip::address_v4::loopback(), PORT));
        listenAddresses.push_back(tcp::endpoint(boost::asio::ip::address_v6::loopback(), PORT));
    }
    bool success = false;
    for (auto ep : listenAddresses) {
        logCritical().nospace() << "Binding http server to " << ep.address().to_string().c_str() << ":" << ep.port();
        try {
            if (!server.listen(QHostAddress(QString::fromStdString(ep.address().to_string())), ep.port())) {
                logCritical() << "  Failed to listen on interface";
            } else {
                success = true;
                break;
            }
        } catch (std::exception &e) {
            logCritical() << "  " << e << "skipping";
        }
    }
    if (!success) {
        logFatal() << "Please pass --bind to tell me which network to listen to";
        return 1;
    }

    try {
        auto ep = app.serverAddressFromArguments(1235);
        if (!ep.hostname.empty())
            handler.addHub(ep);
    } catch (const std::exception &e) {
        logFatal() << e;
        return 1;
    }

    QString confFile;
    if (parser.isSet(conf)) {
        confFile = parser.value(conf);
    } else {
        confFile = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "bitcore-proxy.conf");
        if (confFile.isEmpty()) {
            logCritical() << "No config file (bitcore-proxy.conf) found, assuming defaults and no indexer";
            for (auto dir : QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation)) {
                logInfo() << " - not found in" << dir + '/';
            }
        }
    }
    handler.setConfigFile(confFile.toStdString());

    QObject::connect (&app, SIGNAL(reparseConfig()), &handler, SLOT(onReparseConfig()));

    return app.exec();
}
