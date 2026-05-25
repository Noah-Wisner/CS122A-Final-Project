module display (
    input  wire CLK, 
    output wire LCD_CLK,
    output wire LCD_DEN,
    output reg  [4:0]  LCD_R,
    output reg  [5:0]  LCD_G,
    output reg  [4:0]  LCD_B,

    //new inputs
    input wire   [15:0] pixel,
    output wire [7:0]   pixel_address
);

localparam H_ACTIVE  = 480;
localparam H_TOTAL   = 525;
localparam BAR_WIDTH = H_ACTIVE / 3;
localparam V_ACTIVE  = 272;
localparam V_TOTAL   = 285;

reg [9:0] screen_x = 0;
reg [8:0] screen_y = 0;
reg [1:0] counter = 0;
reg clk_div = 0;

assign pixel_address = ((y % 16) << 4) | (x % 16);

always @(posedge CLK)
    clk_div <= ~clk_div;

assign LCD_CLK = clk_div;
assign LCD_DEN = (screen_x < H_ACTIVE) && (screen_y < V_ACTIVE);

// Pixel counter
always @(posedge LCD_CLK) begin
    if (screen_x == H_TOTAL - 1) 
    begin
        screen_x <= 0;
        if (screen_y == V_TOTAL - 1)
            screen_y <= 0;
        else
            screen_y <= screen_y + 1;
    end else
        screen_x <= screen_x + 1;
end

//Decode colors
always @(*) begin
    LCD_R = 0; LCD_G = 0; LCD_B = 0;
    if (LCD_DEN) begin
        LCD_R = pixel[15:11];
        LCD_G = pixel[10:5];
        LCD_B = pixel[4:0];
    end
end

endmodule