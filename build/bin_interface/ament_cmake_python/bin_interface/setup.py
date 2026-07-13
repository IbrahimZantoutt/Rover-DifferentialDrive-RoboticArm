from setuptools import find_packages
from setuptools import setup

setup(
    name='bin_interface',
    version='0.0.0',
    packages=find_packages(
        include=('bin_interface', 'bin_interface.*')),
)
