#!/bin/bash
# Convenience script to run tests in virtual environment
source venv/bin/activate && pio test -e native "$@"