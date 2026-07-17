#include "types.h"

#include <atomic>
#include <cerrno>
#include <thread>
#include <vector>

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
//
// On Windows this uses the CRT `_pipe()`; the resulting fds live purely inside
// the native addon and are read/written with `_read`/`_write`. They are NEVER
// handed to Node/libuv — an anonymous CRT pipe fd is not a pollable libuv
// handle on Windows (uv_guess_handle -> UNKNOWN), so wrapping it in net.Socket
// or fs.createReadStream throws ERR_INVALID_FD_TYPE / faults the worker. Keeping
// the fd native-only is what makes streaming portable across every platform.
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

// Portable blocking read from a pipe fd. Returns bytes read (>0), 0 at EOF, or
// -1 on error. Retries EINTR. Uses _read on Windows, read on POSIX.
long read_fd(int fd, char* buf, size_t count) {
    for (;;) {
#ifdef _WIN32
        int rv = _read(fd, buf, static_cast<unsigned int>(count));
#else
        ssize_t rv = read(fd, buf, count);
#endif
        if (rv < 0 && errno == EINTR) continue;
        return static_cast<long>(rv);
    }
}

} // namespace

/**
 * Owns the state of one in-flight compressStream() session: the OS pipe, the
 * background compression thread, and the compression result. The raw pipe fds
 * live entirely inside this struct and never cross into JS — the JS side pulls
 * compressed bytes via async reads (see StreamReadWorker), which is the only
 * portable approach on Windows (a CRT pipe fd cannot be adopted by libuv).
 *
 * Lifetime: created by compressMzmlStreamOpen(), wrapped in a Napi::External
 * with a finalizer that joins the thread and frees everything, so it is
 * cleaned up deterministically when the JS handle is GC'd or explicitly closed.
 */
struct CompressStreamSession {
    int read_fd = -1;
    int write_fd = -1;
    std::thread worker;
    std::atomic<bool> started{false};
    // Set by the worker thread when compression finishes. Read by JS only after
    // EOF is observed on the pipe, so no lock is needed (the pipe close/EOF is
    // the happens-before edge between the writer thread and the reader).
    int result = 0;                 // compress_mzml return code (0 = success)
    // These keep the source data alive for the whole background run.
    Napi::ObjectReference handleRef;
    Arguments* args = nullptr;

    ~CompressStreamSession() {
        // Ensure the worker is joined before we tear down (defensive: normally
        // joined once EOF/read-complete is reached).
        if (worker.joinable()) {
            worker.join();
        }
        if (read_fd >= 0) { close_fd(read_fd); read_fd = -1; }
        if (write_fd >= 0) { close_fd(write_fd); write_fd = -1; }
        if (args) { delete args; args = nullptr; }
    }
};

namespace {

void FinalizeSession(Napi::Env /*env*/, CompressStreamSession* session) {
    delete session;  // ~CompressStreamSession joins the thread and frees fds/args
}

// Resolve a Napi::External<CompressStreamSession> argument, or throw.
CompressStreamSession* UnwrapSession(Napi::Env env, const Napi::Value& v) {
    if (!v.IsExternal()) {
        Napi::TypeError::New(env, "compressStream: invalid session handle").ThrowAsJavaScriptException();
        return nullptr;
    }
    return v.As<Napi::External<CompressStreamSession>>().Data();
}

} // namespace

/**
 * AsyncWorker that performs ONE blocking read from the session's pipe on the
 * libuv threadpool and resolves with a Buffer (or null at EOF). This is the
 * pull side of the stream: the JS Readable calls read() again from _read()
 * only after the previous chunk is consumed, so backpressure is honored and
 * memory stays bounded to a single chunk. When EOF is reached, the worker
 * thread is joined and its compression result checked — a failure rejects.
 */
class StreamReadWorker : public Napi::AsyncWorker {
public:
    StreamReadWorker(Napi::Env env, CompressStreamSession* session, size_t chunkSize,
                     Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(env),
          session_(session),
          deferred_(deferred),
          buffer_(chunkSize) {}

    void Execute() override {
        long n = read_fd(session_->read_fd, buffer_.data(), buffer_.size());
        if (n < 0) {
            SetError("compressStream: pipe read failed");
            return;
        }
        bytes_read_ = static_cast<size_t>(n);
        if (n == 0) {
            // EOF: the writer closed the write end. Join the worker thread and
            // surface any compression failure now.
            eof_ = true;
            if (session_->worker.joinable()) {
                session_->worker.join();
            }
            if (session_->result != 0) {
                SetError("compressStream: compression failed");
            }
        }
    }

    void OnOK() override {
        Napi::HandleScope scope(Env());
        if (eof_) {
            deferred_.Resolve(Env().Null());  // null signals EOF to JS
        } else {
            deferred_.Resolve(Napi::Buffer<char>::Copy(Env(), buffer_.data(), bytes_read_));
        }
    }

    void OnError(const Napi::Error& e) override {
        Napi::HandleScope scope(Env());
        deferred_.Reject(e.Value());
    }

private:
    CompressStreamSession* session_;
    Napi::Promise::Deferred deferred_;
    std::vector<char> buffer_;
    size_t bytes_read_ = 0;
    bool eof_ = false;
};

/**
 * compressMzmlStreamOpen(handle, args) -> External<CompressStreamSession>
 *
 * Opens an OS pipe and launches compression on a background std::thread that
 * writes the MSZ output into the write end (forward-only, so a pipe is safe),
 * then closes the write end so the reader observes EOF. Returns an opaque
 * session handle; the JS side pulls bytes with compressMzmlStreamRead(). The
 * raw fds never leave native code, which is what makes this portable.
 */
Napi::Value CompressMzmlStreamOpen(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
        Napi::TypeError::New(env, "compressMzmlStreamOpen: (FileHandle, ArgsObject) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    Arguments* args = NapiObjectToArguments(env, info[1].As<Napi::Object>());

    if (!handle->IsOpen()) {
        delete args;
        Napi::Error::New(env, "compressMzmlStreamOpen: file handle is closed").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (handle->GetDataFormat() == nullptr || handle->GetDivisions() == nullptr) {
        delete args;
        Napi::Error::New(env, "compressMzmlStreamOpen: format/divisions not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    int fds[2];
    if (make_pipe(fds) != 0) {
        delete args;
        Napi::Error::New(env, "compressMzmlStreamOpen: failed to create pipe").ThrowAsJavaScriptException();
        return env.Null();
    }

    auto* session = new CompressStreamSession();
    session->read_fd = fds[0];
    session->write_fd = fds[1];
    session->args = args;                                   // session owns args
    session->handleRef = Napi::Persistent(info[0].As<Napi::Object>());  // keep mapping alive

    // Snapshot the pointers the worker needs so it touches no Napi state.
    char* mapping = static_cast<char*>(handle->GetMapping());
    size_t filesize = handle->GetFilesize();
    data_format_t* df = handle->GetDataFormat();
    divisions_t* divisions = handle->GetDivisions();
    int write_fd = session->write_fd;
    CompressStreamSession* sp = session;

    session->started = true;
    session->worker = std::thread([sp, mapping, filesize, df, divisions, write_fd]() {
        int rv = compress_mzml(mapping, filesize, sp->args, df, divisions, write_fd);
        sp->result = rv;
        // Close the write end so the reader observes EOF (success or failure).
        // Do NOT fsync a pipe (EINVAL). The read side checks sp->result at EOF.
        close_fd(write_fd);
        sp->write_fd = -1;
    });

    return Napi::External<CompressStreamSession>::New(env, session, FinalizeSession);
}

/**
 * compressMzmlStreamRead(session, chunkSize) -> Promise<Buffer | null>
 *
 * Performs one async (threadpool) blocking read from the session pipe. Resolves
 * with a Buffer of up to chunkSize bytes, or null at EOF. Rejects if the read
 * fails or the background compression reported an error. Call repeatedly (from
 * the Readable's _read) until it resolves null.
 */
Napi::Value CompressMzmlStreamRead(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsExternal() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "compressMzmlStreamRead: (session, chunkSize) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    CompressStreamSession* session = UnwrapSession(env, info[0]);
    if (session == nullptr) return env.Null();  // exception already pending

    size_t chunkSize = static_cast<size_t>(info[1].As<Napi::Number>().Int64Value());
    if (chunkSize == 0) chunkSize = 65536;

    auto deferred = Napi::Promise::Deferred::New(env);
    auto* worker = new StreamReadWorker(env, session, chunkSize, deferred);
    worker->Queue();
    return deferred.Promise();
}

} // namespace mscompress
