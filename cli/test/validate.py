import struct
import sys

from lxml import etree
import base64
import zlib
import struct
import pandas as pd
import numpy as np
import traceback


def get_tree(path):
    mzml_file = open(path)
    tree = etree.parse(mzml_file)
    return tree


def get_encoding(tree):
    root = tree.getroot()
    nodes = tree.findall('//mzML/run/spectrumList/spectrum/binaryDataArrayList/binaryDataArray/cvParam',
                         namespaces=root.nsmap)
    for node in nodes:
        accession = node.attrib['accession']
        if accession == 'MS:1000574':
            return 'zlib'
        elif accession == 'MS:1000576':
            return 'nocomp'

def get_datatype(tree):
    root = tree.getroot()
    nodes = tree.findall('//mzML/run/spectrumList/spectrum/binaryDataArrayList/binaryDataArray/cvParam',
                         namespaces=root.nsmap)
    for node in nodes:
        accession = node.attrib['accession']
        if accession == 'MS:1000523':
            return 8 # 64 bit (double)
        elif accession == 'MS:1000521':
            return 4 # 32 bit (float)


def get_binaries(tree):
    root = tree.getroot()
    binaries = []
    for binary in tree.findall(
            '//mzML/run/spectrumList/spectrum/binaryDataArrayList/binaryDataArray/binary',
            namespaces=root.nsmap):
        binaries.append(base64.b64decode(binary.text))
    return binaries


def unpack_floats(data, sizeof_):
    res = []
    fun = None

    def f(i):
        res.append(struct.unpack('f', i)[0])
    def d(i):
        res.append(struct.unpack('<d', i)[0])

    if sizeof_ == 8:
        fun = d
    elif sizeof_ == 4:
        fun = f
    else:
        print('invalid encoding size')
        exit(-1)

    for i in range(0, int(len(data)), sizeof_):
        fun(data[i:i + sizeof_])
    return res


def unpack_all(l, encoding_type, sizeof_):
    mz = []
    intensity = []
    fun = None

    if encoding_type == 'zlib':
        fun = zlib.decompress
    elif encoding_type == 'nocomp':
        def fun(arr): return arr
    else:
        print('invalid encoding_type')
        exit(-1)

    for i in range(0, len(l), 2):
        for b in unpack_floats(fun(l[i]), sizeof_):
            mz.append(b)
        for b in unpack_floats(fun(l[i + 1]), sizeof_):
            intensity.append(b)

    return {'mz': mz, 'int': intensity}


if __name__ == "__main__":
    test = sys.argv[1]
    org = sys.argv[2]
    mz_tolerance = float(sys.argv[3])
    int_tolerance = float(sys.argv[4])

    test_tree = get_tree(test)
    org_tree = get_tree(org)

    test_binaries = unpack_all(get_binaries(test_tree), get_encoding(test_tree), get_datatype(test_tree))
    org_binaries = unpack_all(get_binaries(org_tree), get_encoding(org_tree), get_datatype(org_tree))

    # number of binaries should be the same
    if len(test_binaries) != len(org_binaries):
        print("mismatched number of binaries. org {}, test {}".format(len(test_binaries), len(org_binaries)))
        exit(-1)

    test_df = pd.DataFrame(test_binaries, columns=['mz', 'int'])
    org_df = pd.DataFrame(org_binaries, columns=['mz', 'int'])
    test_df = test_df.rename({'mz': 'test_mz', 'int': 'test_int'}, axis='columns')
    org_df = org_df.rename({'mz': 'org_mz', 'int': 'org_int'}, axis='columns')

    df = test_df.join(org_df)

    # check if the count of mz and int is consistent between test and original
    if df['test_mz'].count() != df['org_mz'].count():
        print("mismatched mz count. org {}, test {}".format(df['test_mz'].count(), df['org_mz'].count()))
        exit(-1)
    if df['test_int'].count() != df['org_int'].count():
        print("mismatched mz count. org {}, test {}".format(df['test_mz'].count(), df['org_mz'].count()))
        exit(-1)

    # check for invalid values
    if df.isnull().values.any():
        print("null values found.")
        exit(-1)
    if np.isinf(df).values.sum() > 0:
        print("inf values found.")
        exit(-1)

    df['%mz_diff'] = abs(df['test_mz'] - df['org_mz']) / df['org_mz']
    df['%int_diff'] = abs(df['test_int'] - df['org_int']) / df['org_mz']

    mz_diff_max = df['%mz_diff'].describe()['max']
    if mz_diff_max > mz_tolerance:
        print('mz tolerance of {} surpassed. (mz max diff: {})'.format(mz_tolerance, mz_diff_max))

    int_diff_max = df['%int_diff'].describe()['max']
    if int_diff_max > int_tolerance:
        print('int tolerance of {} surpassed. (int max diff: {})'.format(int_tolerance, int_diff_max))

    exit(0)
