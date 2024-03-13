const sumMsLevels = (msLevels) => {
    return new Promise ((resolve, reject) => {
        try {
            const summedLevels = {};

            msLevels.forEach((msLevel) => {
                if (msLevel > 2)
                    summedLevels['MSn'] = (summedLevels['MSn'] || 0) + 1;
                else 
                    summedLevels[`MS${msLevel}`] = (summedLevels[`MS${msLevel}`] || 0) + 1;
            });
            resolve(summedLevels);
        }  catch (error) {
            reject(error);
        }
    });
}

const computeSumOfDistances = (dict) => {
    const { start_positions, end_positions } = dict;

    if (!start_positions || !end_positions) {
        throw new Error("Missing start_positions or end_positions in the provided dictionary.");
    }

    if (start_positions.length !== end_positions.length) {
        throw new Error("The length of start_positions and end_positions arrays should be the same.");
    }

    return start_positions.reduce((acc, start, index) => acc + (end_positions[index] - start), 0);
};

const parseMzmlHeader = (fh) => {
    return new Promise((resolve, reject) => {
        fh.read_file(0, 4096) // 4KB should be enough to get the header
        .then(body => {
            try {
                const parser = new DOMParser();
                const xmlDoc = parser.parseFromString(body, "text/xml");
                const source_file = xmlDoc.getElementsByTagName("sourceFile")[0];
                const source_file_name = source_file.getAttribute("name");
                resolve({
                    'source_file_name': source_file_name
                });
            } catch (error) {
                reject(error);
            }
        })
        .catch(reject);
    });
}
