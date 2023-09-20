from lxml import etree
import base64
import os
import numpy as np
import zlib
import pyarrow.parquet as pq
import pyarrow as pa

# MS accessions and default values

ms_access_data_encoding_dict = {"MS:1000519": "32i",
                                "MS:1000520": "16e",
                                "MS:1000521": "32f",
                                "MS:1000522": "64q",
                                "MS:1000523": "64d"}

ms_access_data_compression_dict = {"MS:1000576": "none",
                                   "MS:1000574": "zlib"}

ms_access_data_array_type_dict = {"MS:1000514": "mz",
                                  "MS:1000515": "int",
                                  "MS:1000516": "charge"}

precursor_value_dict = {'precursor_mz': 0.0,
                        'precursor_chg': 2,
                        'precursor_int': 0,
                        'ret_time': 0}

precursor_value_translate_dict = {'MS:1000744': 'precursor_mz',
                                  'MS:1000041': 'precursor_chg',
                                  'MS:1000042': 'precursor_int'}

data_type_ms_access = {"MS:1000519": "data_encoding",
                       "MS:1000520": "data_encoding",
                       "MS:1000521": "data_encoding",
                       "MS:1000522": "data_encoding",
                       "MS:1000523": "data_encoding",
                       "MS:1000576": 'data_compression',
                       "MS:1000574": 'data_compression',
                       "MS:1000514": "data_type",
                       "MS:1000515": "data_type",
                       "MS:1000516": "data_type"}

data_type_dict = {'data_encoding': '32f',
                  'data_compression': 'none',
                  'data_type': 'mz'}

data_type_ms_access_value = {"MS:1000519": "32i",
                             "MS:1000520": "16e",
                             "MS:1000521": "32f",
                             "MS:1000522": "64q",
                             "MS:1000523": "64d",
                             "MS:1000576": 'none',
                             "MS:1000574": 'zlib',
                             "MS:1000514": "mz",
                             "MS:1000515": "int",
                             "MS:1000516": "charge"}

np_dtype_numtype = {'i': np.int32, 'e': np.single, 'f': np.float32, 'q': np.int64, 'd': np.float64}

fields = [pa.field('spec_no', pa.int32()), pa.field('ret_time', pa.float32()), pa.field('mz', pa.float32()),
          pa.field('int', pa.float32()), pa.field('ms_level', pa.int8()), pa.field('precursor', pa.float32()),
          pa.field('charge', pa.int8()), pa.field('mass_bin', pa.int16())] # , pa.field('global_spec_id', pa.int64())
table_schema = pa.schema(fields)

# default values
ms_level = 1
base_peak = 0
total_int = 0
global_spec_id = 0


def mass_bin_function(mass):
    bin_no = int((2e-11 * mass ** 4 - 2e-7 * mass ** 3 - 0.0003 * mass ** 2 + 6.3804 * mass - 3775.2)/5.0)
    return 0 if bin_no < 0 else bin_no


def base64_decoder(base64_data, number_fmt, compress_method, array_type):
    num_type = np_dtype_numtype[number_fmt[-1]]
    # byte_len = int(number_fmt[:-1]) / 8
    decode_base64 = base64.decodebytes(base64_data.encode('ascii'))
    if compress_method == 'zlib':
        decode_base64 = zlib.decompress(decode_base64)
    data = np.frombuffer(decode_base64, dtype=num_type)
    # if array_type == 'int':
    # data = np.log2(np.where(data>0.00001, data, 0.00001)/np.linalg.norm(data))  # performs log only on intensity
    return data


def read_mzml(filename):
    index_dict={}
    global global_spec_id
    start_global_id = global_spec_id
    with open(filename) as fo:
        tree = etree.parse(fo)
        i = 0  # index
        # determine how many floating numbers in total, to create an empty array for fill
        total_numbers = sum([int(each_spectrum.get('defaultArrayLength')) for each_spectrum in tree.findall("//mzML/run/spectrumList/spectrum", namespaces=tree.getroot().nsmap)])
        # print(total_numbers)
        data_array = np.zeros([8, total_numbers], dtype=np.float32)
        # pretty_print_xml_structure(tree)
        # find each spectrum and read data into data_array[spectrum number, retention time, mass, intensity, ms_level, precursor, charge]
        for each_spectrum in tree.findall("//mzML/run/spectrumList/spectrum", namespaces=tree.getroot().nsmap):
            # "/binaryDataArrayList/binaryDataArray"
            spec_no = int(each_spectrum.get('id').split("scan=")[-1])
            spectrum_length = int(each_spectrum.get('defaultArrayLength'))
            if spectrum_length > 0:
                for each_spectrum_cvParam in each_spectrum.getchildren():  # read ms level information
                    ms_access = each_spectrum_cvParam.get('accession')
                    if ms_access == 'MS:1000579':  # MS1 spectrum
                        ms_level = 1
                    elif ms_access == 'MS:1000511':  # ms level
                        ms_level = int(each_spectrum_cvParam.get('value'))
                    elif ms_access == 'MS:1000580':  # MSn spectrum
                        ms_level = 2 if ms_level < 2 else ms_level  # if more than 2, accept, if 1, change to 2
                        # print(data_type, compression_type, array_type, data_length, base_decoder(b64_data, data_type, compression_type, array_type)[:10])
                    if each_spectrum_cvParam.tag == '{http://psi.hupo.org/ms/mzml}scanList':
                        for each_scanList_cvParam in each_spectrum_cvParam.find('scan', namespaces=tree.getroot().nsmap).getchildren():
                            ms_access = each_scanList_cvParam.get('accession')
                            if ms_access == 'MS:1000016':
                                precursor_value_dict['ret_time'] = float(each_scanList_cvParam.get('value'))
                if ms_level >= 2:
                    for precursor_info in each_spectrum.findall('precursorList/precursor/selectedIonList/selectedIon', namespaces=tree.getroot().nsmap):
                        for cv_Param in precursor_info.getchildren():
                            ms_access = cv_Param.get('accession')
                            if ms_access in precursor_value_translate_dict:
                                precursor_value_dict[precursor_value_translate_dict[ms_access]] = float(cv_Param.get('value'))
                        # print(precursor_mz, precursor_chg, precusor_int)
                for each_binary_data_array in each_spectrum.findall('binaryDataArrayList/binaryDataArray', namespaces=tree.getroot().nsmap):
                    for each in each_binary_data_array.getchildren():
                        if each.tag == '{http://psi.hupo.org/ms/mzml}cvParam':
                            ms_access = each.get('accession')
                            if ms_access in data_type_ms_access:
                                data_type_dict[data_type_ms_access[ms_access]] = data_type_ms_access_value[ms_access]
                        elif each.tag == '{http://psi.hupo.org/ms/mzml}binary':
                            b64_data = each.text
                    binary_data = base64_decoder(b64_data, data_type_dict['data_encoding'], data_type_dict['data_compression'], data_type_dict['data_type'])
                    if data_type_dict['data_type'] == 'mz':
                        data_array[2, i:i + spectrum_length] = binary_data
                    elif data_type_dict['data_type'] == 'int':
                        data_array[3, i:i + spectrum_length] = binary_data
                data_array[0, i:i + spectrum_length] = np.full(spectrum_length, spec_no)
                data_array[1, i:i + spectrum_length] = np.full(spectrum_length, precursor_value_dict['ret_time'])
                data_array[4, i:i + spectrum_length] = np.full(spectrum_length, ms_level)
                index_dict[spec_no]=(i,i+spectrum_length)
                #data_array[8, i:i + spectrum_length] = np.full(spectrum_length, global_spec_id)
                if ms_level != 1:
                    data_array[5, i:i + spectrum_length] = np.full(spectrum_length, precursor_value_dict['precursor_mz'])
                    data_array[6, i:i + spectrum_length] = np.full(spectrum_length, precursor_value_dict['precursor_chg'])
                    data_array[7, i:i + spectrum_length] = np.full(spectrum_length, mass_bin_function(precursor_value_dict['precursor_mz']))

                global_spec_id += 1
                i += spectrum_length
    # data_table_dict[spec_no]=[spec_no_array, ret_time_array, mz_array, int_array, ms_level_array, precursor_array, charge_state_array]
    # print(filename, start_global_id, global_spec_id)
    return data_array, index_dict


def write_to_file(mzml_file):
    #parquet_file_name = mzml_file.replace(".mzML",".parquet")
    parquet_file_name = mzml_file + '.parquet'
    array_data, index_dict = read_mzml(mzml_file)
    data_table = pa.Table.from_arrays(array_data, schema=table_schema)
    pq.write_table(data_table, parquet_file_name, compression='ZSTD', use_dictionary=False, data_page_size=2097152)
    return os.path.abspath(parquet_file_name)