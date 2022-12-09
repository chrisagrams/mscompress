import struct
import sys

from lxml import etree
import base64
import zlib
import struct
import pandas as pd
import numpy as np
import traceback


def get_binaries(path):
    mzml_file = open(path)
    tree = etree.parse(mzml_file)
    root = tree.getroot()
    binaries = []
    for binary in tree.findall(
            '//mzML/run/spectrumList/spectrum/binaryDataArrayList/binaryDataArray/binary',
            namespaces=root.nsmap):
        binaries.append(base64.b64decode(binary.text))
    return binaries


def unpack_floats(data):
    res = []
    for i in range(0, int(len(data)/4), 4):
        res.append(struct.unpack('f', data[i:i+4])[0])
    return res


def unpack_all(l):
    mz = []
    intensity = []
    for i in range(0, len(l), 2):
        try:
            for b in unpack_floats(zlib.decompress(l[i])):
                mz.append(b)
            for b in unpack_floats(zlib.decompress(l[i+1])):
                intensity.append(b)
        except zlib.error:
            for b in unpack_floats(l[i]):
                mz.append(b)
            for b in unpack_floats(l[i+1]):
                intensity.append(b)
    return {'mz': mz, 'int': intensity}


if __name__ == "__main__":
    test = sys.argv[1]
    org = sys.argv[2]
    test_binaries = unpack_all(get_binaries(test))
    org_binaries = unpack_all(get_binaries(org))

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

    df['%mz_diff'] = abs(df['test_mz']-df['org_mz'])/df['org_mz']
    df['%int_diff'] = abs(df['test_int']-df['org_int'])/df['org_mz']

    print(df['%mz_diff'].describe())
    print(df['%int_diff'].describe())

    exit(0)