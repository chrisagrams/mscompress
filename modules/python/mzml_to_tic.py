import base64
import io
import sys
import warnings
warnings.simplefilter(action='ignore', category=FutureWarning) # ignore FutureWarning from pyarrow

import seaborn as sns
import numpy as np
import pyarrow as pa
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors

from preview_gen import read_mzml, table_schema


def hex_to_rgb(value: str) -> tuple:
    '''
    Converts hex to rgb colours
    value: string of 6 characters representing a hex colour.
    Returns: list length 3 of RGB values'''
    value = value.strip("#")  # removes hash symbol if present
    lv = len(value)
    return tuple(int(value[i:i + lv // 3], 16) for i in range(0, lv, lv // 3))


def rgb_to_dec(value: tuple) -> list:
    '''
    Converts rgb to decimal colours (i.e. divides each value by 256)
    value: list (length 3) of RGB values
    Returns: list (length 3) of decimal values'''
    return [v / 256 for v in value]


def get_continuous_cmap(hex_list: list, float_list=None) -> mcolors.LinearSegmentedColormap:
    """ creates and returns a color map that can be used in heat map figures.
        If float_list is not provided, colour map graduates linearly between each color in hex_list.
        If float_list is provided, each color in hex_list is mapped to the respective location in float_list.

        Parameters
        ----------
        hex_list: list of hex code strings
        float_list: list of floats between 0 and 1, same length as hex_list. Must start with 0 and end with 1.

        Returns
        ----------
        colour map"""
    rgb_list = [rgb_to_dec(hex_to_rgb(i)) for i in hex_list]
    if float_list:
        pass
    else:
        float_list = list(np.linspace(0, 1, len(rgb_list)))

    cdict = dict()
    for num, col in enumerate(['red', 'green', 'blue']):
        col_list = [[float_list[i], rgb_list[i][num], rgb_list[i][num]] for i in range(len(float_list))]
        cdict[col] = col_list
    cmp = mcolors.LinearSegmentedColormap('my_cmp', segmentdata=cdict, N=256)
    return cmp


def generate_tic_fig(data_table:pa.Table) -> str:
    hex_list = ['#ffffff', '#ffff00', '#ff00ff', '#00df00', '#ff0000', '#000000']  # mimic the xcalibur color
    cmp = get_continuous_cmap(hex_list)
    xic_data_df = data_table.to_pandas()

    # some simple data processing to log the intensity, combine the mz and retention time
    xic_data_df['mz'] = xic_data_df['mz'].round()
    xic_data_df['ret_time'] = xic_data_df['ret_time'].round(1)
    xic_data_df['int'] = xic_data_df['int'].apply(np.log)

    # combine the intensity
    xic_heatmap_df = xic_data_df.pivot_table(index='mz', values='int', columns='ret_time', aggfunc=np.sum, fill_value=0)

    # take cubic root of summed intensity
    xic_heatmap_df = np.cbrt(xic_heatmap_df)

    # set the figure size, y axis range, and splot the heatmap
    plt.figure(figsize=(5.39, 4.02), dpi=150)
    plt.ylim(xic_data_df['mz'].min(), xic_data_df['mz'].max())
    ax = sns.heatmap(xic_heatmap_df, cmap=cmp)
    ax.set_xticklabels(['{:.1f}'.format(float(t.get_text())) for t in ax.get_xticklabels()])
    ax.invert_yaxis()
    plt.tight_layout()

    # Save the figure to a BytesIO object
    buf = io.BytesIO()
    plt.savefig(buf, format='png')
    plt.close()

    # Encode the BytesIO object to base64
    image_base64 = base64.b64encode(buf.getvalue()).decode('utf-8')
    buf.close()

    return image_base64


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python script_name.py <path_to_mzML_file>")
        sys.exit(1)

    mzml_file_name = sys.argv[1]
    array_data, index_dict = read_mzml(mzml_file_name)
    data_table = pa.Table.from_arrays(array_data, schema=table_schema)
    print(generate_tic_fig(data_table))