/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include "syscalls.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using syscalls::NetworkFile;

using syscalls::ErrnoResponse;

using syscalls::OpenRequest;
using syscalls::OpenResponse;

using syscalls::WriteRequest;
using syscalls::WriteResponse;

using syscalls::ReadRequest;
using syscalls::ReadResponse;

using syscalls::FlockRequest;

using syscalls::FcntlRequest;
using syscalls::FcntlFlock;

using syscalls::LseekRequest;
using syscalls::LseekResponse;

using syscalls::FsyncRequest;


// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public NetworkFile::Service {
  Status Open(ServerContext* context, const OpenRequest* request,
                  OpenResponse* reply) override {
    std::cout << "Filename is '" << request->path() << "'\n";

    errno = 0;
    int fd = ::open(request->path().c_str(), request->oflag(), request->mode());
    perror("");

    reply->set_fd(fd);
    reply->set_err(0);
    std::cout << "Sent message " << std::endl;
    
    return Status::OK;
  }

  Status Pwrite(ServerContext* context, const WriteRequest* request,
                  WriteResponse* reply) override {
    std::cout << "Writing to file with fd " << request->fd() << std::endl;
    std::cout << "Writing " << request->buf() << std::endl;
    errno = 0;
    ssize_t got = ::pwrite(request->fd(), request->buf().data(), request->buf().size(), request->offset());

    std::cout << "Wrote " << got << " bytes" << std::endl;
    perror("");
    reply->set_numwritten(got);
    if (got == -1)
      reply->set_err(errno);
    return Status::OK;
  }

  Status Pread(ServerContext* context, const ReadRequest* request,
                  ReadResponse* reply) override {
    std::cout << "Reading from fd " << request->fd() << std::endl;

    std::vector<char> buf(request->nbyte());

    ssize_t got = ::pread(request->fd(), buf.data(), buf.size(), request->offset());
    if (got == -1)
      reply->set_err(errno);
    else
      reply->set_buf(std::string{buf.data(), static_cast<size_t>(got)});
    
    return Status::OK;
  }

  Status Flock(ServerContext* context, const FlockRequest* request,
                  ErrnoResponse* reply) override {
    std::cout << "flocking fd " << request->fd() << std::endl;

    ssize_t got = ::flock(request->fd(), request->operation());
    if (got == -1)
      reply->set_err(errno);

    return Status::OK;
  }

  Status Fcntl(ServerContext* context, const FcntlRequest* request,
                  ErrnoResponse* reply) override {
    
    char buf[256];
    uintptr_t arg = (uintptr_t) buf;
    int cmd = request->cmd();

    if (cmd == F_GETLK || cmd == F_SETLK || cmd == F_SETLKW) {
      fprintf(stderr, "Performing lock operation\n");
      assert(request->has_lk());
      const FcntlFlock &reqlk = request->lk();
      struct flock *lk = reinterpret_cast<struct flock *>(buf);
     
      lk->l_type = reqlk.l_type();
      lk->l_whence = reqlk.l_whence();
      lk->l_start = reqlk.l_start();
      lk->l_len = reqlk.l_len();
      // TOOD: need a better solution to this, it will work on local machine for now.
      lk->l_pid = reqlk.l_pid();
      // lk->l_pid = ::getpid();
    } else if (cmd == F_SETFL || cmd == F_GETFL) {
      arg = request->descriptor_flags();
    }

    int got = ::fcntl(request->fd(), cmd, arg);
    if (got == -1)
      reply->set_err(errno);

    return Status::OK;
  }

  Status Lseek(ServerContext* context, const LseekRequest* request,
                  LseekResponse* reply) override {
    off_t got = ::lseek(request->fd(), request->offset(), request->whence());
    if (got == -1)
      reply->set_err(errno);
    reply->set_offset(got);

    return Status::OK;
  }

  Status Fsync(ServerContext* context, const FsyncRequest* request,
                  ErrnoResponse* reply) override {
    off_t got = ::fsync(request->fd());
    if (got == -1)
      reply->set_err(errno);
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  GreeterServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();

  return 0;
}
