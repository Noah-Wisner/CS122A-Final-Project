module spi (
    input SCK,
    input CS,       // active low
    input MOSI,
    output reg we,
    output reg [7:0] waddr,
    output reg [15:0] wdata
);

reg [15:0] shift_reg = 0;
reg [4:0] bit_count = 0;

reg CS_prev = 1;       // previous value of CS sampled on SCK

always @(posedge SCK) begin
    we <= 0;
    CS_prev <= CS;

    //edge detection for CS
    if (CS_prev && !CS) 
    begin
        bit_count <= 0;
        shift_reg <= 0;
    end

    if (!CS) 
    begin
        shift_reg <= {shift_reg[14:0], MOSI};
        bit_count <= bit_count + 1;

        //update pixel buffer
        if (bit_count == 5'd15)
        begin
            wdata <= {shift_reg[14:0], MOSI};
            we <= 1;
            bit_count <= 0;

            //update frame buffer
            if (waddr != 8'd255)
                waddr <= waddr + 1;
            else
                wadder <= 0;
        end
    end
end

endmodule