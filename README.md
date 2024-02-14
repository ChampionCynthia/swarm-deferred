This project uses a deferred lighting approach that can be implemented parallelly to the forward renderer in the Alien Swarm SDK.

All important source files can be found in _src/materialsystem/_ and the respective _deferred/_ directories in _client/_, _server/_ and _shared/_. Make sure to read the readme in _src/_ which contains further installation information.

## Features ##
The deferred rendering pipeline currently implements the following:
  * cascaded shadow mapping with an atlas
  * dual paraboloid shadow mapping
  * perspective shadow maps
  * different kinds of PCF (color, depth-stencil)
  * cookies via textures
  * cookies via VGUI panels
  * light scattering via light volumes

Lights can be directly loaded from and written to VMF files opposed to placing them in Hammer only.

[![Alien Swarm Deferred Demonstration](https://img.youtube.com/vi/bMjXx-KweIo/0.jpg)](https://www.youtube.com/watch?v=bMjXx-KweIo "Deferred lighting - Alien Swarm SDK")
