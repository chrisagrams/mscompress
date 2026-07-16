#include "types.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

Napi::Value CompressMzml(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsString() || !info[2].IsObject()) {
        Napi::TypeError::New(env, "compressMzml: (FileHandle, outputPath, ArgsObject) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    std::string outputPath = info[1].As<Napi::String>().Utf8Value();
    Arguments* args = NapiObjectToArguments(env, info[2].As<Napi::Object>());

    if (!handle->IsOpen()) {
        delete args;
        Napi::Error::New(env, "compressMzml: file handle is closed").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (handle->GetDataFormat() == nullptr || handle->GetDivisions() == nullptr) {
        delete args;
        Napi::Error::New(env, "compressMzml: format/divisions not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    int output_fd = open_output_file(const_cast<char*>(outputPath.c_str()));
    if (output_fd < 0) {
        delete args;
        Napi::Error::New(env, "compressMzml: failed to open output file: " + outputPath).ThrowAsJavaScriptException();
        return env.Null();
    }

    int rv = compress_mzml(
        static_cast<char*>(handle->GetMapping()),
        handle->GetFilesize(),
        args,
        handle->GetDataFormat(),
        handle->GetDivisions(),
        output_fd
    );

    flush(output_fd);
    close_file(output_fd);
    delete args;

    if (rv != 0) {
        Napi::Error::New(env, "compressMzml: compression failed").ThrowAsJavaScriptException();
        return env.Null();
    }

    return Napi::Boolean::New(env, true);
}

namespace {

// Portable pipe creation. Returns 0 on success and fills fds[0]=read, fds[1]=write.
int make_pipe(int fds[2]) {
#ifdef _WIN32
    // 64 KiB buffer, binary mode (no newline translation).
    return _pipe(fds, 65536, _O_BINARY);
#else
    return pipe(fds);
#endif
}

int close_fd(int fd) {
#ifdef _WIN32
    return _close(fd);
#else
    return close(fd);
#endif
}

} // namespace

/**
 * AsyncWorker that runs the (blocking, internally multi-threaded) compression
 * pipeline on a libuv worker thread, writing the MSZ output straight into the
 * write end of an OS pipe. Because compress_mzml() is strictly forward-only on
 * the output fd (no lseek/pwrite), the fd can safely be a pipe: the JS side
 * drains the read end concurrently. Keeping this off the main thread is
 * mandatory — a full pipe blocks the writer, and if that were the event loop
 * thread the reader could never run and the pipe would deadlock.
 */
class CompressStreamWorker : public Napi::AsyncWorker {
public:
    CompressStreamWorker(Napi::Env env, Napi::Object handleObj, FileHandle* handle,
                         Arguments* args, int write_fd, Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(env),
          handleRef_(Napi::Persistent(handleObj)),
          handle_(handle),
          args_(args),
          write_fd_(write_fd),
          deferred_(deferred) {}

    ~CompressStreamWorker() override {
        if (args_) {
            delete args_;
            args_ = nullptr;
        }
    }

    void Execute() override {
        int rv = compress_mzml(
            static_cast<char*>(handle_->GetMapping()),
            handle_->GetFilesize(),
            args_,
            handle_->GetDataFormat(),
            handle_->GetDivisions(),
            write_fd_
        );

        // Deliberately do NOT fsync() a pipe (it returns EINVAL). Close the
        // write end so the reader sees EOF, regardless of success or failure.
        if (write_fd_ >= 0) {
            close_fd(write_fd_);
            write_fd_ = -1;
        }

        if (rv != 0) {
            SetError("compressMzmlStream: compression failed");
        }
    }

    void OnOK() override {
        Napi::HandleScope scope(Env());
        deferred_.Resolve(Env().Undefined());
    }

    void OnError(const Napi::Error& e) override {
        Napi::HandleScope scope(Env());
        if (write_fd_ >= 0) {
            close_fd(write_fd_);
            write_fd_ = -1;
        }
        deferred_.Reject(e.Value());
    }

private:
    Napi::ObjectReference handleRef_;  // keeps the FileHandle alive for the run
    FileHandle* handle_;
    Arguments* args_;
    int write_fd_;
    Napi::Promise::Deferred deferred_;
};

/**
 * compressMzmlStream(handle, args) -> { readFd: number, done: Promise<void> }
 *
 * Opens an OS pipe, launches compression on a background thread writing to the
 * write end, and returns the read-end fd (for the JS side to wrap in a Readable)
 * plus a promise that settles when compression finishes. On failure the promise
 * rejects; either way the write end is closed so the reader observes EOF.
 */
Napi::Value CompressMzmlStream(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
        Napi::TypeError::New(env, "compressMzmlStream: (FileHandle, ArgsObject) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    Arguments* args = NapiObjectToArguments(env, info[1].As<Napi::Object>());

    if (!handle->IsOpen()) {
        delete args;
        Napi::Error::New(env, "compressMzmlStream: file handle is closed").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (handle->GetDataFormat() == nullptr || handle->GetDivisions() == nullptr) {
        delete args;
        Napi::Error::New(env, "compressMzmlStream: format/divisions not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    int fds[2];
    if (make_pipe(fds) != 0) {
        delete args;
        Napi::Error::New(env, "compressMzmlStream: failed to create pipe").ThrowAsJavaScriptException();
        return env.Null();
    }
    int read_fd = fds[0];
    int write_fd = fds[1];

    auto deferred = Napi::Promise::Deferred::New(env);

    // Worker takes ownership of `args` and `write_fd`; it deletes/closes them.
    auto* worker = new CompressStreamWorker(
        env, info[0].As<Napi::Object>(), handle, args, write_fd, deferred);
    worker->Queue();

    Napi::Object result = Napi::Object::New(env);
    result.Set("readFd", Napi::Number::New(env, read_fd));
    result.Set("done", deferred.Promise());
    return result;
}

} // namespace mscompress
