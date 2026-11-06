# MD5 of an mzML file up to and including </mzML> (the whole file if there is
# none). The indexedmzML index after </mzML> is rebuilt on decompression, so
# it matches the source only when the source's own index was correct, and
# test.mzML's is not (it was cut from a larger file). IndexRebuildTest checks
# the rebuilt index itself.
function(mzml_body_md5 FILE OUT)
    file(READ "${FILE}" CONTENT)
    string(FIND "${CONTENT}" "</mzML>" POS REVERSE)
    if(NOT POS EQUAL -1)
        math(EXPR LEN "${POS} + 7")
        string(SUBSTRING "${CONTENT}" 0 ${LEN} CONTENT)
    endif()
    string(MD5 HASH "${CONTENT}")
    set(${OUT} "${HASH}" PARENT_SCOPE)
endfunction()
