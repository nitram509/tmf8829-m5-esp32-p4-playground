
# Reference documentation

Prefer using the Markdown `.md` files over reading `.pdf` files, when available.
These are pre-converted Markdown files based on the PDF file with the same name.

# Don't touch reference code

In the folder `documentation/code-reference/TMF8829_Driver_Arduino_v1.2.5/` is some reference code from AMS-Osram.
VERY IMPORTANT: Ignore this folder for reading and never mnodify it. You're only allowed to read it, if explicitly prompted.

# Using IDF tools

The `idf.py` requires an environment to be activated.
Always load the environment variables before running a `idf.py` command.
Always use command concatenation with this prefix `source '~/.espressif/tools/activate_idf_v5.4.3.sh &&` before running any `idf.py` command.
Example: `source '~/.espressif/tools/activate_idf_v5.4.3.sh' && idf.py clean` to clean the build.