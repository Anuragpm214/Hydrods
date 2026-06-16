from setuptools import setup, find_packages
import os

with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="hydrods",
    version="2.1.0",
    description="The official Python client for HydroDB",
    long_description=long_description,
    long_description_content_type="text/markdown",
    author="Anurag Panwar",
    url="https://github.com/Anuragpm214/Hydrods",
    packages=find_packages(),
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
        "Intended Audience :: Developers",
        "Topic :: Database",
    ],
    python_requires='>=3.6',
)
