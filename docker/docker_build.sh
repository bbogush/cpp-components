#!/usr/bin/env bash

IMAGE_NAME="cpp-env"
DOCKERFILE="docker/Dockerfile"

# Exit on error
set -e

echo "Start building Docker image."
docker build -t "$IMAGE_NAME" -f "$DOCKERFILE" .
echo "Docker image built successfully."