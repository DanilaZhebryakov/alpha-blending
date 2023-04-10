#include <errno.h>
#include <string.h>
#include <string>
#include <SFML/Graphics.hpp>
#include <immintrin.h>


typedef uint8_t byte;

struct ImageData{
    uint32_t width, height;
    uint32_t* data;
};

void print_si256(__m256i val){
    uint32_t data[8];
    _mm256_storeu_si256((__m256i_u*)data, val);
    for(int i = 0; i < 8;i++){
        printf("%8.8X ", data[i]);
    }
    printf("\n");
}

bool readImagePixel(FILE* file, int bpp, uint32_t* pixel){
    int b_byte = fgetc(file);
    int g_byte = fgetc(file);
    int r_byte = fgetc(file); 
    int a_byte = bpp == 32 ? fgetc(file): 0xFF;

    if(r_byte == -1 || g_byte == -1 || b_byte == -1 || a_byte == -1){
        return false;
    }
    *pixel = r_byte | (g_byte << 8) | (b_byte << 16) | (a_byte << 24);
    return true;
}

bool loadBmpFile(const char* filename, ImageData* image) {
    FILE* img_file = fopen(filename, "rb");
    if (!img_file)
        return false;
    byte* header_mem = (byte*)calloc(127, 1);
    #define RET_ERROR free(header_mem); \
                      fclose(img_file); \
                      return false;

    if (fread(header_mem, 1, 14, img_file) != 14){ //read bitmap file header
        RET_ERROR        
    }
    if (header_mem[0] != 0x42 || header_mem[1] != 0x4D){ //check if it is BMP file
        printf("NOT BMP\n");
        RET_ERROR
    }
    uint32_t pixel_arr_start = *((uint32_t*)(header_mem + 0x0A)); //get start of pixel array from header
    if (fread(header_mem, 1, 4, img_file) != 4){ //first 4 bytes of DIB header contain size of the header (in bytes)
        RET_ERROR
    }
    uint32_t dib_header_size = *((uint32_t*)header_mem) - 4; // first 4 bytes already read
    if(dib_header_size < 16) {
        printf("BAD HEADER\n");
        RET_ERROR
    } //there is old 12-byte format, it is too old to support
    if (fread(header_mem + 4,1, dib_header_size, img_file) != dib_header_size){ //read DIB header
        RET_ERROR
    }
    image->width   = *(uint32_t*)(header_mem + (0x12 - 0x0E));
    image->height  = *(uint32_t*)(header_mem + (0x16 - 0x0E));
    uint32_t img_file_width = image->width;
    image->width   = (image->width + 7) & 0xFFFFFFF8; //alingn to 256 bits

    uint32_t compr = *(uint32_t*)(header_mem + (0x1E - 0x0E));
    uint16_t bpp   = *(uint32_t*)(header_mem + (0x1C - 0x0E));

    if(compr != 0 || (bpp != 32 && bpp != 24)){ // support only 32 or 24 bit-per-pixel no compression
        printf("NON-SUPPORTED FORMAT %u %hu\n" ,compr, bpp);
        RET_ERROR
    }
    uint32_t img_size = image->width * image->height;
    image->data = (uint32_t*)aligned_alloc(256, img_size * 4);

    fseek(img_file, pixel_arr_start, SEEK_SET);

    for(int i = image->height-1; i >= 0; i--){ // for some reason, image is upside-down
        for(int j = 0; j < img_file_width; j++){
            if(!readImagePixel(img_file, bpp, image->data + ((i * image->width) + j))){
                RET_ERROR
            }
        }
    }

    fclose(img_file);
    free(header_mem);
    return true;
    
    #undef RET_ERROR
}

int main(){
    ImageData back_img = {};
    ImageData front_img = {};

    if(!loadBmpFile("table.bmp", &back_img) || !loadBmpFile("cat.bmp", &front_img)){
        perror("Image load error\n");
        return 0;
    }
    printf("%p|%p\n", back_img.data, front_img.data);

    const int win_h = back_img.height;
    const int win_w = back_img.width;
    const int img_h = front_img.height;
    const int img_w = front_img.width;

    int origin_x = 50;
    int origin_y = 64;

    sf::RenderWindow window(sf::VideoMode(win_w, win_h), "");

    uint32_t* texture_mem = (uint32_t*)aligned_alloc(256, win_h * win_w * sizeof(*texture_mem));
    sf::Texture texture;
    texture.create(win_w, win_h);
    sf::Sprite sprite (texture);
    //sprite.move(0, win_h);
    //sprite.scale(1,-1);
    
    sf::Font font;
    if (!font.loadFromFile("/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf")){
        printf("Error while loading font");
        exit(1);
    }
    char time_str[] = "(g/c):XXXXX/XXXXX(us)\0-----";
    sf::Text time_text(time_str, font, 15);
    
    sf::Clock timer;
    int64_t sfmltime = 0;
    int64_t totaltime = 0;
    memcpy(texture_mem, back_img.data, win_h * win_w * 4);


    while (window.isOpen())
    {   
        sf::sleep(sf::milliseconds(10));
        timer.restart();

        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();
        }

        sprintf(time_str, "(g/c):%5.5ld/%5.5ld(us)", sfmltime, totaltime-sfmltime);
        time_text.setString(time_str);

        window.clear();
        texture.update((uint8_t*)texture_mem);
        window.draw(sprite);
        window.draw(time_text);
        window.display();
        sfmltime = timer.getElapsedTime().asMicroseconds();

        const int mtype_size_s = 8;
        __m256i half_mask        = _mm256_set1_epi16(0xFF00);
        __m256i alpha_mask       = _mm256_set1_epi32(0xFF000000);
        __m256i alpha_shfl_mask  = _mm256_set_epi32 (0x801F801F,//  31       1F...... 1B......  ...  03....00
                                                     0x801B801B,//  26       A1B1G1R1 A2B2G2R2  ...  A7B7G7R7
                                                     0x80178017,//  22       |->_-->_ |->_-->_  ...  |->_-->_
                                                     0x80138013,//  18          v   v    v   v  ...     v   v
                                                     0x800F800F,//  14       --A1--A1 --A2--A2  ...  --A7--A7
                                                     0x800B800B,//  10
                                                     0x80078007,//  6
                                                     0x80038003);// 2
        __m256i alpha_inv_const = _mm256_set1_epi16(0x0100);     

        for(int x = 0; x < img_h; x++){
            for(int y = 0; y < img_w; y += mtype_size_s){
                __m256i front_pixels = _mm256_load_si256((__m256i*)(front_img.data + (x * img_w) + y));
                __m256i back_pixels  = _mm256_load_si256((__m256i*)(back_img.data  + ((x + origin_x) * win_w) + (y + origin_y)));

                __m256i alpha_coef   = _mm256_shuffle_epi8(front_pixels  , alpha_shfl_mask); //extract alpha (to low bytes of 16 uint16`s, 2 per pixel) 
                

                __m256i front_pixels_2 = _mm256_and_si256(half_mask, front_pixels);
                __m256i  back_pixels_2 = _mm256_and_si256(half_mask, back_pixels);  //                   pixels_2 (=.=. =.=. =.=. =.=. --)
                front_pixels = _mm256_andnot_si256(half_mask, front_pixels);        //split data in half pixels   (^=^= ^=^= ^=^= ^=^= --)
                 back_pixels = _mm256_andnot_si256(half_mask, back_pixels );
                
                front_pixels_2 = _mm256_srli_si256(front_pixels_2, 1); //shift values in pixels_2 so they can be read as pi16
                back_pixels_2  = _mm256_srli_si256(back_pixels_2 , 1);

                front_pixels    = _mm256_mullo_epi16  (front_pixels, alpha_coef  ); // * alpha  
                front_pixels_2  = _mm256_mullo_epi16  (front_pixels_2, alpha_coef); // = (%X%X %X%X %X%X %X%X --- ) for both

                alpha_coef      = _mm256_sub_epi16    (alpha_inv_const, alpha_coef); //invrt alpha

                back_pixels    = _mm256_mullo_epi16  (back_pixels  , alpha_coef);
                back_pixels_2  = _mm256_mullo_epi16  (back_pixels_2, alpha_coef);


                back_pixels   = _mm256_add_epi16(back_pixels  , front_pixels);
                back_pixels_2 = _mm256_add_epi16(back_pixels_2, front_pixels_2);
                

                back_pixels   = _mm256_srli_epi16(back_pixels,8);          //pixels = originally low bytes. Move high byte to low with >>
                back_pixels_2 = _mm256_and_si256(half_mask, back_pixels_2); //pixels_2 originally were high bytes. Just keep their high values

                back_pixels = _mm256_add_epi8(back_pixels, back_pixels_2);
                back_pixels = _mm256_or_si256(back_pixels, alpha_mask);
                _mm256_store_si256((__m256i*)(texture_mem + ((x + origin_x) * win_w) + (y + origin_y)), back_pixels);

            }
        }        

        totaltime = timer.getElapsedTime().asMicroseconds();
        
    }
}