FROM python:3.12-slim
RUN pip install --no-cache-dir matplotlib pandas
COPY analyze.py /app/analyze.py
WORKDIR /app
CMD ["python", "analyze.py"]
