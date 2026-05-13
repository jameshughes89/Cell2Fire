# to build: docker build -t cell2fire:latest .
# local test: docker run -it --mount source=$(pwd),destination=/cell2fire,type=bind cell2fire:latest
FROM python:3.12-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libboost-all-dev \
    libeigen3-dev \
    libgl1 \
    libglib2.0-0 \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /cell2fire

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt \
    && pip install --no-cache-dir flake8 pytest pytest-cov
