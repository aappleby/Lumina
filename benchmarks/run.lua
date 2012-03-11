print("Running mandelbro.lua 1000")
arg = {}
arg[1] = 1000
dofile("mandelbrot.lua")

arg[1] = 5500
dofile("specralnorm.lua")