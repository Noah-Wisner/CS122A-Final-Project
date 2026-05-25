module top (
    input  wire CLK,

    output wire LCD_CLK,
    output wire LCD_DEN,
    output wire [4:0] LCD_R,
    output wire [5:0] LCD_G,
    output wire [4:0] LCD_B,

    input wire SCK
    input wire MOSI,
    input wire CS
);

    //display read wires
    wire [7:0]  pixel_address;
    wire [15:0] pixel;

    //SPI write wires
    wire we;
    wire [7:0] waddr;
    wire [15:0] wdata;

    // Display
    display display_inst (
        .CLK(CLK),

        .LCD_CLK(LCD_CLK),
        .LCD_DEN(LCD_DEN),
        .LCD_R(LCD_R),
        .LCD_G(LCD_G),
        .LCD_B(LCD_B),

        .pixel(pixel),
        .pixel_address(pixel_address)
    );

    // SPI
    spi spi_inst (
        .SCK(SCK),

        .CS(CS),
        .MOSI(MOSI),
        
        .we(we),
        .waddr(waddr),
        .wdata(wdata)
    );

    // Sprite data
    dp_buffer buffer (
        .clk(LCD_CLK),
        
        .we(we),
        .waddr(waddr),
        .wdata(wdata),

        .raddr(pixel_address),
        .rdata(pixel)
    );

endmodule