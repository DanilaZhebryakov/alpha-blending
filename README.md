this program performs alpha blending of two images. Its main purpose is actually not to actually do the task, but to write this algorithm using avx instructions to blend multiple pixels at once and test the perfomance

## bmp format

Images are read from bmp format and the first problematic task is to decode that. According to [wikipedia][file_formats] multiple formats exist, some of them use compression and this is what I don`t want to mess with, so support only 24/32 bpp uncompressed formats. **loadBmpFile()** checks that given file has a valid bmp header and uses supported format. Image width and height can be read from DIB header, the offset of image data - from file header. There is also a problem that pixels in the file are not stored in the same format sfml uses (so  **readImagePixel()** is required instead of just simple **fread()** I rely on builtin C file buffers for code simplicity), and image is stored 'upside down'.

## blending algorithm
An idea of alpha blending is simple:\
each color component = ((top\*alpha) + (bottom\*(255-alpha)))/255 \
I think the accuracy would be good enough if 256 is used instead of 255. While this can make background a bit(1/255) visible behind full-opacity image, it makes calculations a lot simpler

Algorithm works per-pixel, so basic idea is to compute multiple pixels at once using AVX instructions.

AVX registers can store 256 bits of data. This is 8 pixels (uint32). One color component is represented by 1 byte inside this register, but we need to multiply these values by 8-bit alpha value, so the result can take 2 bytes. Obviously, we need to place these color values into two 256-bit regiseters in some way.

One possible way to do that is to take two sets of 4 pixels (using bitwise AND and bit shift and cast them to 16-bit values, calculate them separately then after calculations shift them (to divide by 256), cast back to 32-bit and combine using bit shift and bitwise OR)

I think more effective way of doing that is to split each pixel in two halves (ABGR -> A-G- + -B-R), then shift one of the values 1 byte right. This way, no casts are needed and split and combine operation become simpler.

Split:
(-- = 00)

    pixels      =  (AABBGGRR AABBGGRR ...
    pixels_2    = pixels AND  (FF00FF00 ....)
                =  (AA--GG-- AA--GG-- ...)
    pixels_2    = pixels_2 >> 8
                =  (--AA--RR --AA--RR ...)
    pixels_1    = pixels AND ~(FF00FF00 ....)
                =  (--BB--RR --BB--RR ...)

Combine (also /256)
(X = some data we don`t use)

    pixels_1    =  (AAXXGGXX ...)
    pixels_2    =  (BBXXRRXX ...)

    pixels      = pixels_1 AND (FF00FF00)
                =  (AA--GG-- ...)
    pixels_2    = pixels_2 >> 8 within each 16-bit block
                =  (--BB--RR ...)
    pixels      = pixels OR pixels_2
                =  (AABBGGRR ...)

Also, this algorithm allows to use a single register to store alpha values for multiplication instead of two. With this, alpha values can be extracted directly from source pixels (using shuffle) in the start of this algorithm instead of just before multiplication, which may improve parallelism.

Another problem with loading and stoing data is alignment. Buffers need to be aligned by 32 bytes and row sizes/offsets need to be multiples of 8. To break this restriction, more complex algorithm is required.


Calculations are done in a loop and displayed on screen each time. Time of last loop is displayed on screen. Graphics processing time is tracked separately and also displayed.

Calculation times are around 100 ㎲. Graphics processing times are around 800 ㎲. In this case, blending algorithm takes less time than display part. It is quite possible that memory access speed is a limiting factor in this case.


[file_formats]: https://en.wikipedia.org/wiki/BMP_file_format#DIB_header_(bitmap_information_header)