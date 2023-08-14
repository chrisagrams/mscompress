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
