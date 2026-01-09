from mscompress import read, create_mszx, MSZFile, MSZXFile
from mscompress.annotations import PSMReader

if __name__ == "__main__":
    # Read MSZ file
    msz = read('/Users/chrisgrams/Downloads/HSA.msz')

    if not isinstance(msz, MSZFile):
        raise ValueError("Input file is not a valid MSZ file.")

    # Read search results
    # fragger_results = SearchResultsReader()

    # Create MSZX file with search results
    create_mszx(
        msz,
        output_path='/Users/chrisgrams/Downloads/HSA_with_results.mszx',
        annotations=['/Users/chrisgrams/Downloads/HSA/HSA.pepXML'],
        description='MSZX file with peptide-spectrum matches from pepXML annotations.'
    )

    # Read MSZX file and print PSMs
    # mszx = read('/Users/chrisgrams/Downloads/HSA_with_results.mszx')
    mszx = MSZXFile.open('/Users/chrisgrams/Downloads/HSA_with_results.mszx')

    if not isinstance(mszx, MSZXFile):
        raise ValueError("Output file is not a valid MSZX file.")

    print(mszx.manifest)