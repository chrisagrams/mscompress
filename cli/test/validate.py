import base64
import struct
import sys
import zlib

from lxml.etree import XMLSyntaxError
from tqdm import tqdm
from lxml import etree


def get_iterator(path):
    # Parse the file to get the root and extract the namespace
    context = etree.iterparse(path, events=('start', 'end'))
    _, root = next(context)  # Get the root element
    nsmap = root.nsmap
    namespace = nsmap.get(None)  # Get the default namespace
    root.clear()
    return context, namespace


def get_encoding(iterator, namespace):
    for event, elem in iterator:
        if event == 'start' and elem.tag == f"{{{namespace}}}cvParam":
            accession = elem.attrib.get('accession')
            if accession == 'MS:1000574':
                return 'zlib'
            elif accession == 'MS:1000576':
                return 'nocomp'
        elem.clear()  # Clear element to free memory


def get_datatype(iterator, namespace):
    for event, elem in iterator:
        if event == 'start' and elem.tag == f"{{{namespace}}}cvParam":
            accession = elem.attrib.get('accession')
            if accession == 'MS:1000523':
                return 8  # 64 bit (double)
            elif accession == 'MS:1000521':
                return 4  # 32 bit (float)
        elem.clear()  # Clear element to free memory


def unpack_floats(data, sizeof_):
    if sizeof_ not in (4, 8):
        raise ValueError('invalid encoding size')

    unpack_format = 'f' if sizeof_ == 4 else 'd'
    return [struct.unpack(unpack_format, data[i:i + sizeof_])[0] for i in range(0, len(data), sizeof_)]


def unpack_all(data, encoding_type, sizeof_):
    if encoding_type == 'zlib':
        data = zlib.decompress(data)
    elif encoding_type != 'nocomp':
        raise ValueError('invalid encoding_type')

    return unpack_floats(data, sizeof_)


def compare_binary_data(test_binary_data, org_binary_data, encoding, datatype, delta):
    test_binary = unpack_all(base64.b64decode(test_binary_data), encoding, datatype)
    org_binary = unpack_all(base64.b64decode(org_binary_data), encoding, datatype)

    if len(test_binary) != len(org_binary):
        raise ValueError("Binary data lengths do not match")

    for i, j in zip(test_binary, org_binary):
        if abs(i - j) > delta:
            raise ValueError("Binary data exceeded delta tolerance.")


def traverse_and_compare(test_iterator, org_iterator, namespace, encoding, datatype, mz_tolerance, int_tolerance, sync_tag='spectrum'):
    sync_tag_with_ns = f"{{{namespace}}}{sync_tag}"

    curr_tolerance = mz_tolerance

    for (test_event, test_elem), (org_event, org_elem) in tqdm(zip(test_iterator, org_iterator)):
        if test_event != org_event or test_elem.tag != org_elem.tag:
            raise ValueError("Tree shapes do not match")

        elif test_event == 'start' and test_elem.tag == f"{{{namespace}}}cvParam":
            accession = test_elem.attrib.get('accession')
            if accession == 'MS:1000514':   # currently m/z array
                curr_tolerance = mz_tolerance
            elif accession == 'MS:1000515':     # currently intensity array
                curr_tolerance = int_tolerance

        elif test_event == 'end' and test_elem.tag == sync_tag_with_ns:
            test_binary_data = test_elem.find(f'.//{{{namespace}}}binary')
            org_binary_data = org_elem.find(f'.//{{{namespace}}}binary')

            if test_binary_data is None or org_binary_data is None:
                raise ValueError("Binary data arrays do not match")

            compare_binary_data(test_binary_data.text, org_binary_data.text, encoding, datatype, delta=curr_tolerance)

            test_elem.clear()  # Remove from memory once done
            org_elem.clear()


def compare_mzml(test_mzml, org_mzml, mz_tolerance, int_tolerance):
    try:
        test_tree, test_namespace = get_iterator(test_mzml)
        org_tree, org_namespace = get_iterator(org_mzml)
    except XMLSyntaxError:
        print("XML syntax error.")
        return False

    if test_namespace != org_namespace:
        print("Namespaces do not match.")
        sys.exit(1)

    test_encoding = get_encoding(test_tree, test_namespace)
    org_encoding = get_encoding(org_tree, org_namespace)

    # Reset iterators before new traversal
    try:
        test_tree, test_namespace = get_iterator(test_mzml)
        org_tree, org_namespace = get_iterator(org_mzml)
    except XMLSyntaxError:
        print("XML syntax error.")
        return False

    test_datatype = get_datatype(test_tree, test_namespace)
    org_datatype = get_datatype(org_tree, org_namespace)

    if test_encoding != org_encoding:
        print("Encodings do not match.")
        return False

    if test_datatype != org_datatype:
        print("Datatypes do not match.")
        return False

    print(f"Encoding: {test_encoding}")
    print(f"Datatype size: {test_datatype}")

    # Reset iterators before new traversal
    try:
        test_tree, test_namespace = get_iterator(test_mzml)
        org_tree, org_namespace = get_iterator(org_mzml)
    except XMLSyntaxError:
        print("XML syntax error.")
        return False

    try:
        traverse_and_compare(test_tree, org_tree, test_namespace, test_encoding, test_datatype, mz_tolerance,
                             int_tolerance)
    except ValueError as e:
        print(e)
        return False
    except Exception as e:
        print(f"Generic exception: {e}")
        return False

    return True


if __name__ == "__main__":
    test = sys.argv[1]
    org = sys.argv[2]
    mz_tolerance = float(sys.argv[3])
    int_tolerance = float(sys.argv[4])

    if len(sys.argv) < 5:
        print("Usage: python validate.py <test> <org> <mz_tolerance> <int_tolerance>")
        sys.exit(1)

    print(f"Test file: {test}")
    print(f"Original file: {org}")

    if not compare_mzml(test, org, mz_tolerance, int_tolerance):
        sys.exit(1)
    sys.exit(0)

