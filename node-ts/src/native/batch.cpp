/**
 * NAPI wrapper around the shared C batch writer (src/batch.c).
 *
 * Drives the same incremental writer the CLI uses, so an archive written from
 * Node is byte-identical to one the CLI produces from the same inputs and
 * settings. Entries stream straight into the archive fd — no temp .msz staging
 * — which is why the output must be a seekable regular file.
 *
 * `add()` runs on the libuv threadpool because compressing one entry is a
 * whole multi-threaded compression run; blocking the event loop for it would
 * stall the process. The writer is stateful and appends sequentially, so
 * overlapping adds would interleave their payload bytes and corrupt the
 * archive — a busy flag rejects concurrent calls rather than silently
 * producing a broken file.
 */

#include <string>

#include "types.h"

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

namespace {

/**
 * Compress one mzML into the archive off the event loop.
 *
 * Opens and maps the source on the worker thread; the mapping never crosses
 * back into JS, so there is no lifetime hazard with a JS-side handle being
 * closed mid-flight.
 */
class BatchAddWorker : public Napi::AsyncWorker {
   public:
    BatchAddWorker(Napi::Env env, Napi::Object self, batch_writer_t* writer,
                   std::string sourcePath, std::string entryName,
                   bool haveEntryName, Arguments* args, bool* busy,
                   Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(env),
          selfRef_(Napi::Persistent(self)),
          writer_(writer),
          sourcePath_(std::move(sourcePath)),
          entryName_(std::move(entryName)),
          haveEntryName_(haveEntryName),
          args_(args),
          busy_(busy),
          deferred_(deferred) {}

    ~BatchAddWorker() override {
        delete args_;
        selfRef_.Reset();
    }

    void Execute() override {
        int fd = open_input_file(const_cast<char*>(sourcePath_.c_str()));
        if (fd < 0) {
            SetError("batch add: could not open " + sourcePath_);
            return;
        }

        size_t filesize = get_filesize(const_cast<char*>(sourcePath_.c_str()));
        void* mapping = get_mapping(fd);
        if (mapping == nullptr || filesize == 0) {
            if (mapping != nullptr) remove_mapping(mapping, filesize);
            close_file(fd);
            SetError("batch add: could not map " + sourcePath_);
            return;
        }

        index_ = batch_writer_add_mzml(
            writer_, haveEntryName_ ? entryName_.c_str() : nullptr,
            sourcePath_.c_str(), mapping, filesize, args_);

        remove_mapping(mapping, filesize);
        close_file(fd);

        if (index_ < 0) {
            SetError("batch add: could not add " + sourcePath_ +
                     " (not an mzML file, or compression failed)");
        }
    }

    void OnOK() override {
        *busy_ = false;
        deferred_.Resolve(Napi::Number::New(Env(), index_));
    }

    void OnError(const Napi::Error& e) override {
        *busy_ = false;
        deferred_.Reject(e.Value());
    }

   private:
    // Strong reference to the JS BatchWriter, so it cannot be collected (and
    // its destructor cannot abort the writer) while Execute() is running.
    Napi::ObjectReference selfRef_;
    batch_writer_t* writer_;
    std::string sourcePath_;
    std::string entryName_;
    bool haveEntryName_;
    Arguments* args_;
    bool* busy_;
    Napi::Promise::Deferred deferred_;
    int index_ = -1;
};

}  // namespace

/**
 * Stateful handle to an in-progress .mszx archive. Exposed to JS as the
 * `BatchWriter` class; see node-ts/src/mszx/mszx-batch-writer.ts for the
 * ergonomic wrapper.
 */
class BatchWriter : public Napi::ObjectWrap<BatchWriter> {
   public:
    static Napi::Function Init(Napi::Env env) {
        return DefineClass(env, "BatchWriter",
                           {
                               InstanceMethod("add", &BatchWriter::Add),
                               InstanceMethod("addAnnotation", &BatchWriter::AddAnnotation),
                               InstanceMethod("setJoinKey", &BatchWriter::SetJoinKey),
                               InstanceMethod("setDescription", &BatchWriter::SetDescription),
                               InstanceMethod("setExtraJson", &BatchWriter::SetExtraJson),
                               InstanceMethod("finish", &BatchWriter::Finish),
                               InstanceMethod("abort", &BatchWriter::Abort),
                           });
    }

    explicit BatchWriter(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<BatchWriter>(info) {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "BatchWriter: (outputPath: string) expected")
                .ThrowAsJavaScriptException();
            return;
        }

        path_ = info[0].As<Napi::String>().Utf8Value();
        writer_ = batch_writer_open(path_.c_str());
        if (writer_ == nullptr) {
            Napi::Error::New(env,
                             "BatchWriter: could not open '" + path_ +
                                 "' for writing (the output must be a seekable "
                                 "regular file, not a pipe)")
                .ThrowAsJavaScriptException();
        }
    }

    ~BatchWriter() override {
        // An un-finished writer reaching GC would otherwise leak the fd and
        // strand a partial archive on disk.
        if (writer_ != nullptr) {
            batch_writer_abort(writer_);
            writer_ = nullptr;
        }
    }

   private:
    bool EnsureOpen(Napi::Env env) {
        if (writer_ == nullptr) {
            Napi::Error::New(env, "BatchWriter: writer is finished or aborted")
                .ThrowAsJavaScriptException();
            return false;
        }
        return true;
    }

    Napi::Value Add(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!EnsureOpen(env)) return env.Undefined();

        if (info.Length() < 3 || !info[0].IsString() || !info[2].IsObject()) {
            Napi::TypeError::New(
                env, "add: (sourcePath: string, entryName: string|null, args) expected")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (busy_) {
            Napi::Error::New(env,
                             "BatchWriter: an add() is already in flight; entries "
                             "are appended sequentially and must not overlap")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        std::string sourcePath = info[0].As<Napi::String>().Utf8Value();
        bool haveEntryName = info[1].IsString();
        std::string entryName = haveEntryName ? info[1].As<Napi::String>().Utf8Value() : "";
        Arguments* args = NapiObjectToArguments(env, info[2].As<Napi::Object>());

        auto deferred = Napi::Promise::Deferred::New(env);
        busy_ = true;
        auto* worker = new BatchAddWorker(env, info.This().As<Napi::Object>(), writer_,
                                          std::move(sourcePath), std::move(entryName),
                                          haveEntryName, args, &busy_, deferred);
        worker->Queue();
        return deferred.Promise();
    }

    Napi::Value AddAnnotation(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!EnsureOpen(env)) return env.Undefined();

        if (info.Length() < 5 || !info[0].IsNumber() || !info[1].IsBuffer() ||
            !info[2].IsString() || !info[3].IsString()) {
            Napi::TypeError::New(env,
                                 "addAnnotation: (entryIndex, data: Buffer, name: "
                                 "string, format: string, compressed: boolean, "
                                 "numRecords?) expected")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        int entryIndex = info[0].As<Napi::Number>().Int32Value();
        auto data = info[1].As<Napi::Buffer<char>>();
        std::string name = info[2].As<Napi::String>().Utf8Value();
        std::string format = info[3].As<Napi::String>().Utf8Value();
        bool compressed = info[4].ToBoolean().Value();
        int64_t numRecords =
            (info.Length() > 5 && info[5].IsNumber())
                ? static_cast<int64_t>(info[5].As<Napi::Number>().Int64Value())
                : -1;

        if (batch_writer_add_annotation(writer_, entryIndex, name.c_str(), data.Data(),
                                        data.Length(), format.c_str(), compressed ? 1 : 0,
                                        numRecords) != 0) {
            Napi::Error::New(env, "addAnnotation: could not add '" + name + "'")
                .ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    Napi::Value SetJoinKey(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!EnsureOpen(env)) return env.Undefined();
        if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsString()) {
            Napi::TypeError::New(env, "setJoinKey: (entryIndex, joinKey) expected")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }
        std::string key = info[1].As<Napi::String>().Utf8Value();
        if (batch_writer_set_join_key(writer_, info[0].As<Napi::Number>().Int32Value(),
                                      key.c_str()) != 0) {
            Napi::Error::New(env, "setJoinKey: failed").ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    Napi::Value SetDescription(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!EnsureOpen(env)) return env.Undefined();
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "setDescription: (description: string) expected")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }
        std::string description = info[0].As<Napi::String>().Utf8Value();
        if (batch_writer_set_description(writer_, description.c_str()) != 0) {
            Napi::Error::New(env, "setDescription: failed").ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    Napi::Value SetExtraJson(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!EnsureOpen(env)) return env.Undefined();
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "setExtraJson: (json: string) expected")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }
        std::string json = info[0].As<Napi::String>().Utf8Value();
        if (batch_writer_set_extra_json(writer_, json.c_str()) != 0) {
            Napi::Error::New(env, "setExtraJson: failed").ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    Napi::Value Finish(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!EnsureOpen(env)) return env.Undefined();
        if (busy_) {
            Napi::Error::New(env, "BatchWriter: cannot finish while an add() is in flight")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // batch_writer_finish frees the writer whether or not it succeeds.
        int rc = batch_writer_finish(writer_);
        writer_ = nullptr;
        if (rc != 0) {
            Napi::Error::New(env, "BatchWriter: could not finalize archive '" + path_ + "'")
                .ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    Napi::Value Abort(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (writer_ != nullptr) {
            batch_writer_abort(writer_);
            writer_ = nullptr;
        }
        return env.Undefined();
    }

    batch_writer_t* writer_ = nullptr;
    std::string path_;
    bool busy_ = false;
};

Napi::Function BatchWriterInit(Napi::Env env) { return BatchWriter::Init(env); }

}  // namespace mscompress
