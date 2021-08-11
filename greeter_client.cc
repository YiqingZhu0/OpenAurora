#include <fcntl.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>

#include <cassert>

#include <iostream>
#include <memory>
#include <string>
#include <map>

#include <grpcpp/grpcpp.h>

#include "syscalls.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
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


std::unique_ptr<NetworkFile::Stub> stub;

static void create_stub() {
  std::string target_str("localhost:50051");
  stub = NetworkFile::NewStub(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  if (stub)
    return;
  
  std::cerr << "Couldn't start the grpc client" << std::endl;
  ::abort();
}

using LocalFD = int;
using RemoteFD = int;
std::map<LocalFD, RemoteFD> fdMap;

std::optional<LocalFD> fakefd;

using sys_open_t = int(const char *, int, mode_t);
using sys_pwrite_t = ssize_t(int, const void *, size_t, off_t);
using sys_pread_t = ssize_t(int, void *, size_t, off_t);
using sys_flock_t = int(int, int);
using sys_fcntl_t = int(int, int, uintptr_t);
using sys_lseek_t = off_t(int, off_t, int);
using sys_fsync_t = int(int);
using sys_stat_t = int(const char *, struct stat *);
using sys_close_t = int(int);

extern "C" int open(const char *path, int oflag, ...) {
  fprintf(stderr, "open %s\n", path);
  static sys_open_t *sys_open;
  if (!sys_open)
    sys_open = (sys_open_t*) dlsym(RTLD_NEXT, "open");
  assert(sys_open);

  int mode = 0;
  if (oflag & O_CREAT) {
    va_list ap;
    va_start(ap, oflag);
    mode = va_arg(ap, int);
    va_end(ap);
  }

  if (strncmp(path, "./test/", 7))
    return sys_open(path, oflag, mode);

  //const char *dot = strrchr(path, '.');
  //if (!dot || !strcmp(path, "mysql.ibd") || strncmp(dot, ".ibd", 4))

  // In the test database...

  if (!stub) create_stub();

  std::cerr << "Filename is " << path << std::endl;

  OpenRequest request;
  request.set_path(path);
  request.set_oflag(oflag);
  request.set_mode(mode);

  OpenResponse reply;
  ClientContext context;

  Status status = stub->Open(&context, request, &reply);

  if (status.ok()) {
    if (int err = reply.err())
      errno = err;
    int remotefd = reply.fd();
    if (remotefd == -1)
      return -1;

    // create a real local fd so we dont do operations on a real local fd.
    if (!fakefd) {
      fakefd = sys_open((std::string(path) + ".fake").c_str(), O_CREAT | O_WRONLY, 0200);
      perror("");
    }
    fdMap.insert({*fakefd, remotefd});
    fprintf(stderr, "fd %d corresponds to %s\n", *fakefd, path);
    return *fakefd;
  }

  std::cerr << "Returning -1?\n";
  return -1;
}

extern "C" int close(int localfd) {
  fprintf(stderr, "close %d\n", localfd);
  static sys_close_t *sys_close;
  if (!sys_close)
    sys_close = (sys_close_t*) dlsym(RTLD_NEXT, "close");
  assert(sys_close);

  auto found = fdMap.find(localfd);
  if (found == fdMap.end())
    return sys_close(localfd);

  if (!stub) create_stub();
  fprintf(stderr, "Closing our fd\n");
  return 0;
}

extern "C" ssize_t pwrite(int localfd, const void *buf, size_t nbyte, off_t offset) {
  static sys_pwrite_t *sys_pwrite;
  if (!sys_pwrite)
    sys_pwrite = (sys_pwrite_t*) dlsym(RTLD_NEXT, "pwrite");
  assert(sys_pwrite);
  
  auto found = fdMap.find(localfd);
  if (found == fdMap.end())
    return sys_pwrite(localfd, buf, nbyte, offset);

  if (!stub) create_stub();

  WriteRequest request;
  request.set_fd(found->second);
  request.set_buf(std::string((const char *)buf, nbyte));
  request.set_offset(offset);

  WriteResponse reply;
  ClientContext context;

  Status status = stub->Pwrite(&context, request, &reply);
  if (!status.ok())
    return -1;

  if (int err = reply.err())
    errno = err;
  return reply.numwritten();
}

extern "C" ssize_t pread(int localfd, void *buf, size_t nbyte, off_t offset) {
  // fprintf(stderr, "pread %d\n", localfd);
  static sys_pread_t *sys_pread;
  if (!sys_pread)
    sys_pread = (sys_pread_t*) dlsym(RTLD_NEXT, "pread");
  assert(sys_pread);

  auto found = fdMap.find(localfd);
  if (found == fdMap.end())
    return sys_pread(localfd, buf, nbyte, offset);

  if (!stub) create_stub();

  ReadRequest request;
  request.set_fd(found->second);
  request.set_nbyte(nbyte);
  request.set_offset(offset);

  ReadResponse reply;
  ClientContext context;

  Status status = stub->Pread(&context, request, &reply);
  if (!status.ok())
    return -1;
  
  if (int err = reply.err())
    errno = err;

  memcpy(buf, reply.buf().data(), reply.buf().size());
  return reply.buf().size();
}

extern "C" int flock(int localfd, int operation) {
  fprintf(stderr, "flock %d\n", localfd);
  static sys_flock_t *sys_flock;
  if (!sys_flock)
    sys_flock = (sys_flock_t*) dlsym(RTLD_NEXT, "flock");
  assert(sys_flock);

  auto found = fdMap.find(localfd);
  if (found == fdMap.end())
    return sys_flock(localfd, operation);

  if (!stub) create_stub();

  FlockRequest request;
  request.set_fd(found->second);
  request.set_operation(operation);

  ErrnoResponse reply;
  ClientContext context;

  Status status = stub->Flock(&context, request, &reply);
  if (!status.ok())
    return -1;
  
  if (int err = reply.err()) {
    errno = err;
    return -1;
  }
  return 0;
}

extern "C" int fcntl(int localfd, int cmd, ...) {
  static sys_fcntl_t *sys_fcntl;
  if (!sys_fcntl)
    sys_fcntl = (sys_fcntl_t*) dlsym(RTLD_NEXT, "fcntl");
  assert(sys_fcntl);

  va_list ap;
  va_start(ap, cmd);
  uintptr_t nextreg = va_arg(ap, uintptr_t );
  va_end(ap);

  auto found = fdMap.find(localfd);
  if (found == fdMap.end())
    return sys_fcntl(localfd, cmd, nextreg);

  if (!stub) create_stub();


  FcntlRequest request;
  request.set_fd(found->second);
  request.set_cmd(cmd);

  if (cmd == F_GETLK || cmd == F_SETLK || cmd == F_SETLKW) {
    return sys_fcntl(localfd, cmd, nextreg);
    #if 0
    struct flock *lk = reinterpret_cast<struct flock *>(nextreg);
    FcntlFlock *reqlk = request.mutable_lk();
    reqlk->set_l_type(lk->l_type);
    reqlk->set_l_whence(lk->l_whence);
    reqlk->set_l_start(lk->l_start);
    reqlk->set_l_len(lk->l_len);
    //assert(lk->l_pid == getpid() && "mysqld using different pid for lock");
    reqlk->set_l_pid(lk->l_pid);
    #endif
  } else if (cmd == F_SETFL || cmd == F_GETFL) {
    request.set_descriptor_flags(nextreg);
  }

  ErrnoResponse reply;
  ClientContext context;

  Status status = stub->Fcntl(&context, request, &reply);
  if (!status.ok())
    return -1;

  if (int err = reply.err()) {
    errno = err;
    return -1;
  }

  return 0;
}

extern "C" off_t lseek(int localfd, off_t offset, int whence) {
  static sys_lseek_t *sys_lseek;
  if (!sys_lseek)
    sys_lseek = (sys_lseek_t*) dlsym(RTLD_NEXT, "lseek");
  assert(sys_lseek);

  auto found = fdMap.find(localfd);
  if (found == fdMap.end())
    return sys_lseek(localfd, offset, whence);

  if (!stub) create_stub();

  LseekRequest request;
  request.set_fd(found->second);
  request.set_offset(offset);
  request.set_whence(whence);

  LseekResponse reply;
  ClientContext context;

  Status status = stub->Lseek(&context, request, &reply);
  if (!status.ok())
    return -1;
  
  if (int err = reply.err())
    errno = err;

  return reply.offset();
}

extern "C" int fsync(int localfd) {
  static sys_fsync_t *sys_fsync;
  if (!sys_fsync)
    sys_fsync = (sys_fsync_t*) dlsym(RTLD_NEXT, "fsync");
  assert(sys_fsync);

  auto found = fdMap.find(localfd);
  if (found == fdMap.end())
    return sys_fsync(localfd);

  if (!stub) create_stub();

  FsyncRequest request;
  request.set_fd(found->second);

  ErrnoResponse reply;
  ClientContext context;

  Status status = stub->Fsync(&context, request, &reply);
  if (!status.ok())
    return -1;

  if (int err = reply.err()) {
    errno = err;
    return -1;
  }

  return 0;
}

#if 1
extern "C" int stat(const char *path, struct stat *buf) {
  std::fprintf(stderr, "Stat called on path %s\n", path);
  static sys_stat_t *sys_stat;
  // hack on macos, this will need to be changed later.
  if (!sys_stat)
    sys_stat = (sys_stat_t*) dlsym(RTLD_NEXT, "stat$INODE64");
  assert(sys_stat);

  return sys_stat(path, buf);
}
#endif
