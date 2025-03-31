#!/bin/bash 

WAYLAND_PROTOCOLES_GIT="https://gitlab.freedesktop.org/wayland/wayland-protocols.git"
WLR_DATA_CTL_GIT="https://gitlab.freedesktop.org/wlroots/wlr-protocols.git"

SCRIPT_PATH=$0 
SCRIPT_PATH="$(realpath "$(dirname "${SCRIPT_PATH}")")"
SCRIPT_ROOT_PATH="${SCRIPT_PATH}/.."
echo $SCRIPT_ROOT_PATH
OUTPUT_HEADER_DIR="${SCRIPT_ROOT_PATH}/internal/xdg"
OUTPUT_SRC_DIR="${SCRIPT_ROOT_PATH}/src/xdg"
WAYLAND_PROTOCOLES_DIR="${SCRIPT_PATH}/wayland-protocols"
WLR_DATA_DIR="${SCRIPT_PATH}/wlr-protocols"
WAYLAND_PROTOCOLES=(xdg-shell xdg-dialog xdg-decoration)
# Check if script should run
if [[ -n "$(ls -A ${OUTPUT_HEADER_DIR})"   ]]; then 
    echo "${OUTPUT_HEADER_DIR} is not empty, Nothing to do "
    echo "Exiting ..."
    exit 0 
else 
    echo "Generating Wayland protocols"
fi


echo "Cloning the Wayland Protocols repository..."
if [[ ! -d "$WAYLAND_PROTOCOLES_DIR" ]]; then
    git clone "$WAYLAND_PROTOCOLES_GIT" "${WAYLAND_PROTOCOLES_DIR}/"
else
    echo "Wayland protocols directory already exists, skipping clone."
fi

# Ensure OUTPUT_DIR exists
mkdir -p "$OUTPUT_DIR"

for i in "${WAYLAND_PROTOCOLES[@]}"; do 
  echo "Searching for: $i"

  # Get first match
  p=$(find "${WAYLAND_PROTOCOLES_DIR}" -type f -name "${i}*.xml" | head -n 1)

  # Check if a file was found
  if [[ -z "$p" ]]; then
    echo "No matching XML file found for $i."
    continue
  fi

  echo "Found: $p"
  echo "--- Using wayland-scanner"

  # Generate output path
  NP="${OUTPUT_HEADER_DIR}/$(basename "$p")"
  NP="${NP%.*}.h"  # Change extension to .h
  NC="${OUTPUT_SRC_DIR}/$(basename "$p")"
  NC="${NC%.*}.c" 
  echo "Output: $NP"
  echo "Output $NC"

  # Run wayland-scanner safely
  if wayland-scanner client-header "$p" "$NP"; then
    echo "Successfully generated $NP"
  else
    echo "Error processing $p"
  fi
  if wayland-scanner private-code "$p" "$NC"; then
    echo "Successfully generated $NC"
  else
    echo "Error processing $p"
  fi
done 

echo "Cloning repo for WLR Data Protocols..."
if [[ ! -d "$WLR_DATA_DIR" ]]; then
    git clone "${WLR_DATA_CTL_GIT}" "${WLR_DATA_DIR}/"
else
    echo "WLR protocols directory already exists, skipping clone."
fi

WLR_DATA_CTL_NAME_XML="wlr-data-control"
p=$(find "${WLR_DATA_DIR}" -type f -name "${WLR_DATA_CTL_NAME_XML}*.xml" | head -n 1)

# Check if the file was found
if [[ -z "$p" ]]; then
    echo "Error: No WLR data-control XML file found."
    exit 1
fi

NP="${OUTPUT_HEADER_DIR}/$(basename "$p")"
NP="${NP%.*}.h"  # Change extension to .h
NC="${OUTPUT_SRC_DIR}/$(basename "$p")"
NC="${NC%.*}.c"
echo "Output: $NP"
echo "Output: $NC"
if wayland-scanner client-header "$p" "$NP"; then
    echo "Successfully generated $NP"
else
    echo "Error processing $p"
    exit 1
fi

if wayland-scanner private-code "$p" "$NC"; then
    echo "Successfully generated $NC"
else
    echo "Error processing $p"
    exit 1
fi


echo "cleaning environment " 
rm -rf ${WAYLAND_PROTOCOLES_DIR}
rm -rf ${WLR_DATA_DIR}
echo "Done."



