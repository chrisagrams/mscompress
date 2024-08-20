from mscompress import get_num_threads, get_filesize, MZMLFile, read

mzml_path = "/Users/chrisgrams/Notes/mscompress_dev/test/lossless_tests/163-3A_1.mzML"
msz_path = "~/Downloads/hek_std2_64bit.msz"
if __name__ == "__main__":
    # print(get_num_threads())
    # print(get_filesize(mzml_path))
    # print(MZMLFile(mzml_path))
    # print(MZMLFile(mzml_path).describe())
    # mzml_file = read(mzml_path)
    # print(mzml_file)
    # print(mzml_file.describe())
    # print(mzml_file.describe()['format'].to_dict())

    # spectra = mzml_file.spectra
    # # print(spectra[0])

    # # Get info of first spectrum
    # print(spectra[0].size)
    # print(spectra[0].mz)
    # print(spectra[0].intensity)
    # print(spectra[0].peaks)

    # # Iterate over all spectra
    # for spectrum in spectra:
    #     print(spectrum)
    
    

    msz_file = read(msz_path)
    print(msz_file)
    print(msz_file.describe())
    print(msz_file.describe()['format'].to_dict())

    # print(msz_file.positions.scans)
    spectra = msz_file.spectra
    for spectrum in spectra:
        print(spectrum)
    print(len(spectra[0].mz))
    print(spectra[0].mz)
    print(len(spectra[0].intensity))
    print(spectra[0].intensity)
    print(spectra[0].peaks)


