from mscompress import read, create_mszx, MSZFile, MSZXFile

if __name__ == "__main__":
    # Read MSZ file
    msz = read('/Users/chrisgrams/Downloads/HSA.msz')

    if not isinstance(msz, MSZFile):
        raise ValueError("Input file is not a valid MSZ file.")

    # Create MSZX file with search results
    create_mszx(
        msz,
        output_path='/Users/chrisgrams/Downloads/HSA_with_results.mszx',
        annotations=['/Users/chrisgrams/Downloads/HSA/HSA.pepXML'],
        description='MSZX file with peptide-spectrum matches from pepXML annotations.'
    )

    # Read MSZX file and print PSMs
    mszx = MSZXFile.open('/Users/chrisgrams/Downloads/HSA_with_results.mszx')

    if not isinstance(mszx, MSZXFile):
        raise ValueError("Output file is not a valid MSZX file.")

    print(mszx.manifest)

    # Select scans that have annotations
    annotated_scan_nums = []
    if mszx.annotations is not None:
        for annotation in mszx.annotations:
            if annotation is not None:
                annotated_scan_nums.append(annotation.scan_number)

    print(len(annotated_scan_nums))

    # Take first 100 annotated scans and print PSMs
    annotated_scan_nums = annotated_scan_nums[:100]

    # Create a new MSZX with extracted scans
    mszx_subset = mszx.extract(
        output='../test/data/test.mszx',
        scan_numbers=annotated_scan_nums
    )

    print(len(mszx_subset.spectra))

    if mszx_subset.annotations is not None:
        print(len(mszx_subset.annotations))


    

