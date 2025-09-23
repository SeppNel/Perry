# Perry
Cross-platform discord-like chat application completly self hosted.

> [!WARNING]  
> This is under active development. It is not ready for general use yet. Many basic features are yet not implemented and the ones that are may break at any point.

<img width="999" height="621" alt="image" src="https://github.com/user-attachments/assets/0375b156-0fd3-4fb6-b06c-b33c44971530" />


# Usage
TODO

## Configuration
TODO

### Example config file
TODO

# Building
## Client
### Linux
Install the dependencies and in the client folder run:
```
mkdir build
cd build
cmake ..
make
```

#### Dependencies
For Fedora:  
`sudo dnf install qt6-qtbase-devel`

### Windows
TODO


## Server
### Linux
Install the dependencies and in the server folder run:
```
mkdir build
cd build
cmake ..
make
```

#### Dependencies
For Fedora:  
`sudo dnf install unixODBC-devel mysql-connector-odbc`

### Windows
TODO


# Acknowledgements
[nanodbc](https://nanodbc.github.io/nanodbc/) a small library that makes ODBC API programming easy and fun again  
[Christoph Hilchenbach](https://github.com/hilch) for his [Bcrypt](https://github.com/hilch/Bcrypt.cpp) C++ wrapper  
[libsound Team](http://libsound.io/) for their amazing audio library  
