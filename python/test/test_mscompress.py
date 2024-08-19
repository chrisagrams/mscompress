from mscompress import get_num_threads, get_filesize, MZMLFile, read

mzml_path = "~/Downloads/120323QEx2_RS1_20nl-min_0k1HeLa_5h_01.mzML"
msz_path = "/Users/chrisgrams/Notes/mscompress_dev/cli/163-3A_1.msz"
if __name__ == "__main__":
    # print(get_num_threads())
    # print(get_filesize(mzml_path))
    # print(MZMLFile(mzml_path))
    # print(MZMLFile(mzml_path).describe())
    mzml_file = read(mzml_path)
    print(mzml_file)
    print(mzml_file.describe())
    print(mzml_file.describe()['format'].to_dict())

    spectra = mzml_file.spectra
    print(spectra[0])

    for spectrum in spectra:
        print(spectrum)

    # msz_file = read(msz_path)
    # print(msz_file)
    # print(msz_file.describe())
    # print(msz_file.describe()['format'].to_dict())
