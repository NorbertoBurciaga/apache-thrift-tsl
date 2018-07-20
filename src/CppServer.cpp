/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <boost/filesystem.hpp>

#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/PlatformThreadFactory.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include "thrift/server/TNonblockingServer.h"
#include "thrift/transport/TSSLSocket.h"
#include "thrift/transport/TNonblockingSSLServerSocket.h"
#include <iostream>
#include <event.h>

#include "../gen-cpp/Calculator.h"

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::concurrency;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::server;

using namespace tutorial;
using namespace shared;

class CalculatorHandler : public CalculatorIf {
public:
  CalculatorHandler() {}

  void ping() { cout << "ping()" << endl; }

  int32_t add(const int32_t n1, const int32_t n2) {
    cout << "add(" << n1 << ", " << n2 << ")" << endl;
    return n1 + n2;
  }

  int32_t calculate(const int32_t logid, const Work& work) {
    cout << "calculate(" << logid << ", " << work << ")" << endl;
    int32_t val;

    switch (work.op) {
    case Operation::ADD:
      val = work.num1 + work.num2;
      break;
    case Operation::SUBTRACT:
      val = work.num1 - work.num2;
      break;
    case Operation::MULTIPLY:
      val = work.num1 * work.num2;
      break;
    case Operation::DIVIDE:
      if (work.num2 == 0) {
        InvalidOperation io;
        io.whatOp = work.op;
        io.why = "Cannot divide by 0";
        throw io;
      }
      val = work.num1 / work.num2;
      break;
    default:
      InvalidOperation io;
      io.whatOp = work.op;
      io.why = "Invalid Operation";
      throw io;
    }

    SharedStruct ss;
    ss.key = logid;
    ss.value = to_string(val);

    log[logid] = ss;

    return val;
  }

  void getStruct(SharedStruct& ret, const int32_t logid) {
    cout << "getStruct(" << logid << ")" << endl;
    ret = log[logid];
  }

  void zip() { cout << "zip()" << endl; }

protected:
  map<int32_t, SharedStruct> log;
};

/*
  CalculatorIfFactory is code generated.
  CalculatorCloneFactory is useful for getting access to the server side of the
  transport.  It is also useful for making per-connection state.  Without this
  CloneFactory, all connections will end up sharing the same handler instance.
*/
class CalculatorCloneFactory : virtual public CalculatorIfFactory {
 public:
  virtual ~CalculatorCloneFactory() {}
  virtual CalculatorIf* getHandler(const ::apache::thrift::TConnectionInfo& connInfo)
  {
    stdcxx::shared_ptr<TSocket> sock = stdcxx::dynamic_pointer_cast<TSocket>(connInfo.transport);
    cout << "Incoming connection\n";
    cout << "\tSocketInfo: "  << sock->getSocketInfo() << "\n";
    cout << "\tPeerHost: "    << sock->getPeerHost() << "\n";
    cout << "\tPeerAddress: " << sock->getPeerAddress() << "\n";
    cout << "\tPeerPort: "    << sock->getPeerPort() << "\n";
    return new CalculatorHandler;
  }
  virtual void releaseHandler( ::shared::SharedServiceIf* handler) {
    delete handler;
  }
};

class ListenEventHandler : public TServerEventHandler {
  public:
    ListenEventHandler(Mutex* mutex) : listenMonitor_(mutex), ready_(false) {}

    void preServe() /* override */ {
      Guard g(listenMonitor_.mutex());
      ready_ = true;
      listenMonitor_.notify();
    }

    Monitor listenMonitor_;
    bool ready_;
};

boost::filesystem::path keyDir;
boost::filesystem::path certFile(const std::string& filename) {
  return keyDir / filename;
}

class Runner : public apache::thrift::concurrency::Runnable {
  public:
  int port;
  stdcxx::shared_ptr<event_base> userEventBase;
  stdcxx::shared_ptr<CalculatorProcessorFactory> processor;
  stdcxx::shared_ptr<server::TNonblockingServer> server;
  stdcxx::shared_ptr<ListenEventHandler> listenHandler;
  stdcxx::shared_ptr<TSSLSocketFactory> pServerSocketFactory;
  stdcxx::shared_ptr<transport::TNonblockingSSLServerSocket> socket;
  Mutex mutex_;

  Runner() {
    listenHandler.reset(new ListenEventHandler(&mutex_));
  }

  virtual void run() {
    // When binding to explicit port, allow retrying to workaround bind failures on ports in use
    int retryCount = port ? 10 : 0;
//    pServerSocketFactory = createServerSocketFactory();  

    pServerSocketFactory.reset(new TSSLSocketFactory());
    pServerSocketFactory->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
    pServerSocketFactory->loadCertificate(certFile("server.crt").string().c_str());
    pServerSocketFactory->loadPrivateKey(certFile("server.key").string().c_str());
    pServerSocketFactory->server(true);

    startServer(retryCount);
  }

  void readyBarrier() {
    // block until server is listening and ready to accept connections
    Guard g(mutex_);
    while (!listenHandler->ready_) {
      listenHandler->listenMonitor_.wait();
    }
  }

  private:
  void startServer(int retry_count) {
    try {
      socket.reset(new transport::TNonblockingSSLServerSocket(port, pServerSocketFactory));
      server.reset(new server::TNonblockingServer(processor, socket));
      server->setServerEventHandler(listenHandler);
      server->setNumIOThreads(1);
      if (userEventBase) {
        server->registerEvents(userEventBase.get());
      }
      server->serve();
    } catch (const transport::TTransportException&) {
      if (retry_count > 0) {
        ++port;
        startServer(retry_count - 1);
      } else {
        throw;
      }
    }
  }
};

struct EventDeleter {
  void operator()(event_base* p) { event_base_free(p); }
};

void canCommunicate(int serverPort) {
  stdcxx::shared_ptr<TSSLSocketFactory> pClientSocketFactory;

  pClientSocketFactory.reset(new TSSLSocketFactory());
  pClientSocketFactory->authenticate(true);
  pClientSocketFactory->loadCertificate(certFile("client.crt").string().c_str());
  pClientSocketFactory->loadPrivateKey(certFile("client.key").string().c_str());
  pClientSocketFactory->loadTrustedCertificates(certFile("CA.pem").string().c_str());

  stdcxx::shared_ptr<TSSLSocket> socket = pClientSocketFactory->createSocket("localhost", serverPort);
  stdcxx::shared_ptr<TTransport> transport(new TFramedTransport(socket));
  stdcxx::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
  CalculatorClient client(protocol);

  try {
    socket->open();

    client.ping();
    cout << "ping()" << endl;

    cout << "1 + 1 = " << client.add(1, 1) << endl;

    Work work;
    work.op = Operation::DIVIDE;
    work.num1 = 1;
    work.num2 = 0;

    try {
      client.calculate(1, work);
      cout << "Whoa? We can divide by zero!" << endl;
    } catch (InvalidOperation& io) {
      cout << "InvalidOperation: " << io.why << endl;
      // or using generated operator<<: cout << io << endl;
      // or by using std::exception native method what(): cout << io.what() << endl;
    }

    work.op = Operation::SUBTRACT;
    work.num1 = 15;
    work.num2 = 10;
    int32_t diff = client.calculate(1, work);
    cout << "15 - 10 = " << diff << endl;

    // Note that C++ uses return by reference for complex types to avoid
    // costly copy construction
    SharedStruct ss;
    client.getStruct(ss, 1);
    cout << "Received log: " << ss << endl;

    transport->close();
  } catch (TException& tx) {
    cout << "ERROR: " << tx.what() << endl;
  }
}

int main() {
  stdcxx::shared_ptr<event_base> userEventBase;
  stdcxx::shared_ptr<server::TNonblockingServer> server;
  stdcxx::shared_ptr<TSSLSocketFactory> pServerSocketFactory;
  stdcxx::shared_ptr<transport::TNonblockingSSLServerSocket> socket;
  stdcxx::shared_ptr<CalculatorProcessorFactory> processor = stdcxx::make_shared<CalculatorProcessorFactory>(stdcxx::make_shared<CalculatorCloneFactory>());
  stdcxx::shared_ptr<apache::thrift::concurrency::Thread> thread;

#ifdef __linux__
  // OpenSSL calls send() without MSG_NOSIGPIPE so writing to a socket that has
  // disconnected can cause a SIGPIPE signal...
  signal(SIGPIPE, SIG_IGN);
#endif

  TSSLSocketFactory::setManualOpenSSLInitialization(true);
  apache::thrift::transport::initializeOpenSSL();

  keyDir = boost::filesystem::current_path().parent_path().parent_path() / "keys";

  event_base* eb = event_base_new();
  userEventBase.reset(eb, EventDeleter());

  stdcxx::shared_ptr<Runner> runner(new Runner);
  runner->port = 0;
  runner->processor =  processor;
  runner->userEventBase = userEventBase;

  apache::thrift::stdcxx::scoped_ptr<apache::thrift::concurrency::ThreadFactory> threadFactory(
    new apache::thrift::concurrency::PlatformThreadFactory(
#if !USE_BOOST_THREAD && !USE_STD_THREAD
      concurrency::PlatformThreadFactory::OTHER, concurrency::PlatformThreadFactory::NORMAL,
      1,
#endif
      false));
  thread = threadFactory->newThread(runner);
  cout << "Starting the server..." << endl;
  thread->start();
  runner->readyBarrier();

  server = runner->server;

  cout << "Port : " << server->getListenPort() << endl;

  canCommunicate(server->getListenPort());
//  canCommunicate(server->getListenPort());
//  canCommunicate(server->getListenPort());

  if (server) {
    server->stop();
  }
  if (thread) {
    thread->join();
  }

  apache::thrift::transport::cleanupOpenSSL();
#ifdef __linux__
  signal(SIGPIPE, SIG_DFL);
#endif
  cout << "Done." << endl;

//int stop;
//cin >> stop;

  return runner->port;
}
