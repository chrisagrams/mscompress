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
