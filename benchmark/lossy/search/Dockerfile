FROM fcyucn/fragpipe
COPY search.sh /scripts/search.sh
COPY closed_fragger.params /config/closed_fragger.params
# MSFragger requires an academic license and must be provided externally.
# Mount the JAR into the container at /ext/MSFragger.jar or set FRAGGER_JAR.
ENTRYPOINT ["bash", "/scripts/search.sh"]
