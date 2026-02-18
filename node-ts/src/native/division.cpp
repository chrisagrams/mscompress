#include "types.h"
#include <cstring>

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

Napi::Value ScanMzml(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "scanMzml: FileHandle expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    if (!handle->IsOpen()) {
        Napi::Error::New(env, "scanMzml: file handle is closed").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (handle->GetDataFormat() == nullptr) {
        Napi::Error::New(env, "scanMzml: data format not initialized. Call getDataFormat first.").ThrowAsJavaScriptException();
        return env.Null();
    }

    int flags = MSLEVEL | SCANNUM | RETTIME;
    division_t* positions = scan_mzml(
        static_cast<char*>(handle->GetMapping()),
        handle->GetDataFormat(),
        static_cast<long>(handle->GetFilesize()),
        flags
    );

    if (positions == nullptr) {
        Napi::Error::New(env, "scanMzml: failed to scan mzML file").ThrowAsJavaScriptException();
        return env.Null();
    }

    handle->SetPositions(positions);
    return CreateDivisionObject(env, positions);
}

Napi::Value ReadMszDivisions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "readMszDivisions: FileHandle expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    if (!handle->IsOpen()) {
        Napi::Error::New(env, "readMszDivisions: file handle is closed").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Read footer
    footer_t* footer = read_footer(handle->GetMapping(), static_cast<long>(handle->GetFilesize()));
    if (footer == nullptr) {
        Napi::Error::New(env, "readMszDivisions: failed to read footer").ThrowAsJavaScriptException();
        return env.Null();
    }
    handle->SetFooter(footer);

    // Read divisions from footer
    divisions_t* divisions = read_divisions(handle->GetMapping(), footer->divisions_t_pos, footer->n_divisions);
    if (divisions == nullptr) {
        Napi::Error::New(env, "readMszDivisions: failed to read divisions").ThrowAsJavaScriptException();
        return env.Null();
    }
    handle->SetDivisions(divisions);
    handle->SetMmapBacked(true);

    // Flatten divisions into a single division for positions
    division_t* positions = flatten_divisions(divisions);
    handle->SetPositions(positions);

    // Read block length queues
    block_len_queue_t* xml_blk = read_block_len_queue(
        handle->GetMapping(),
        static_cast<long>(footer->xml_blk_pos),
        static_cast<long>(footer->mz_binary_blk_pos)
    );
    handle->SetXmlBlockLens(xml_blk);

    block_len_queue_t* mz_blk = read_block_len_queue(
        handle->GetMapping(),
        static_cast<long>(footer->mz_binary_blk_pos),
        static_cast<long>(footer->inten_binary_blk_pos)
    );
    handle->SetMzBlockLens(mz_blk);

    block_len_queue_t* inten_blk = read_block_len_queue(
        handle->GetMapping(),
        static_cast<long>(footer->inten_binary_blk_pos),
        static_cast<long>(footer->divisions_t_pos)
    );
    handle->SetIntenBlockLens(inten_blk);

    // Allocate ZSTD decompression context
    ZSTD_DCtx* dctx = alloc_dctx();
    handle->SetDctx(dctx);

    // Build result object
    Napi::Object result = Napi::Object::New(env);
    result.Set("positions", CreateDivisionObject(env, positions));
    result.Set("footer", CreateFooterObject(env, footer));

    return result;
}

Napi::Value PrepareDivisions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
        Napi::TypeError::New(env, "prepareDivisions: (FileHandle, ArgsObject) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    Arguments* args = NapiObjectToArguments(env, info[1].As<Napi::Object>());

    if (handle->GetPositions() == nullptr) {
        delete args;
        Napi::Error::New(env, "prepareDivisions: positions not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    division_t* positions = handle->GetPositions();
    long blocksize = args->blocksize;
    int threads = args->threads;

    long n_divisions = determine_n_divisions(positions->size, blocksize);

    // Mirror Python logic for handling edge cases
    if (n_divisions > positions->mz->total_spec) {
        n_divisions = positions->mz->total_spec;
        if (n_divisions == 0) n_divisions = 1;
    } else if (n_divisions < threads) {
        long effective = threads;
        if (effective > positions->mz->total_spec) {
            effective = positions->mz->total_spec;
            if (effective == 0) effective = 1;
        }
        n_divisions = effective;
    }

    divisions_t* divisions = create_divisions(positions, n_divisions);
    handle->SetDivisions(divisions);
    handle->SetMmapBacked(false);

    // If threads > n_divisions originally, update blocksize
    if (threads > n_divisions) {
        args->blocksize = get_division_size_max(divisions);
    }

    // Return the updated blocksize
    Napi::Object result = Napi::Object::New(env);
    result.Set("nDivisions", Napi::Number::New(env, n_divisions));
    result.Set("blocksize", Napi::Number::New(env, static_cast<double>(args->blocksize)));

    delete args;
    return result;
}

} // namespace mscompress
