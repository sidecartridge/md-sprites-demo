; Firmware loader from cartridge
; (C) 2023-2025 by Diego Parrilla
; License: GPL v3

; Some technical info about the header format https://www.atari-forum.com/viewtopic.php?t=14086

; $FA0000 - CA_MAGIC. Magic number, always $abcdef42 for ROM cartridge. There is a special magic number for testing: $fa52235f.
; $FA0004 - CA_NEXT. Address of next program in cartridge, or 0 if no more.
; $FA0008 - CA_INIT. Address of optional init. routine. See below for details.
; $FA000C - CA_RUN. Address of program start. All optional inits are done before. This is required only if program runs under GEMDOS.
; $FA0010 - CA_TIME. File's time stamp. In GEMDOS format.
; $FA0012 - CA_DATE. File's date stamp. In GEMDOS format.
; $FA0014 - CA_SIZE. Lenght of app. in bytes. Not really used.
; $FA0018 - CA_NAME. DOS/TOS filename 8.3 format. Terminated with 0 .

; CA_INIT holds address of optional init. routine. Bits 24-31 aren't used for addressing, and ensure in which moment by system init prg. will be initialized and/or started. Bits have following meanings, 1 means execution:
; bit 24: Init. or start of cartridge SW after succesfull HW init. System variables and vectors are set, screen is set, Interrupts are disabled - level 7.
; bit 25: As by bit 24, but right after enabling interrupts on level 3. Before GEMDOS init.
; bit 26: System init is done until setting screen resolution. Otherwise as bit 24.
; bit 27: After GEMDOS init. Before booting from disks.
; bit 28: -
; bit 29: Program is desktop accessory - ACC .
; bit 30: TOS application .
; bit 31: TTP

ROM4_ADDR			equ $FA0000
FRAMEBUFFER_A_ADDR	equ ($FB0000 - 32000)
FRAMEBUFFER_B_ADDR	equ ($FB0000 - 64000)
COPIED_CODE_OFFSET	equ $00010000 ; The offset should be below the screen memory
COPIED_CODE_SIZE	equ $00005000
PRE_RESET_WAIT		equ $0000FFFF ; Wait this many cycles before resetting the computer
SCREEN_A_BASE_ADDR  equ $70000 ; The screen memory address for the framebuffer
SCREEN_B_BASE_ADDR  equ $78000 ; The screen memory address for the framebuffer
COPYCODE_A_SRCADDR  equ (ROM4_ADDR + $600) ; The address of the code to copy the framebuffer A
COPYCODE_B_SRCADDR  equ (ROM4_ADDR + $2600) ; The address of the code to copy the framebuffer B
COPYCODE_A_ADDR     equ (SCREEN_A_BASE_ADDR - COPIED_CODE_OFFSET + $600) ; The address of the code to copy the framebuffer A
COPYCODE_B_ADDR     equ (SCREEN_A_BASE_ADDR - COPIED_CODE_OFFSET + $2600) ; The address of the code to copy the framebuffer B
COPYCODE_SIZE		equ $1F48


_conterm			equ $484	; Conterm device number

ROMCMD_START_ADDR:        equ $FB0000					  ; We are going to use ROM3 address
CMD_BOOSTER		    	  equ ($ABCD) 					  ; The go to Booster command
CMD_VBLANK		    	  equ ($DCBA) 					  ; The vertical blank command
CMD_START_DEMO		  	  equ ($E1A8) 					  ; The start demo command

LISTENER_ADDR		      equ (ROM4_ADDR + $5F8)		  ; The address of the listener
REMOTE_RESET		      equ $1					      ; The device ask to reset the

_dskbufp                  equ $4c6                        ; Address of the disk buffer pointer    
_p_cookies                equ $5a0    					  ; pointer to the system Cookie-Jar

COOKIE_JAR_STE		   	  equ $00010000                   ; STE computer
COOKIE_JAR_MEGASTE        equ $00010010                   ; Mega STE computer

; Copy 32000 bytes from $00FA0300 to $00780000 with the STE blitter
; Uses one blit: X_COUNT=640 words, Y_COUNT=25 lines, linear (Y_INC=0)

BLT_BASE        		equ     $FFFF8A00
BLT_SRC_XINC    		equ     (BLT_BASE+$20)
BLT_SRC_YINC    		equ     (BLT_BASE+$22)
BLT_SRC_ADDR    		equ     (BLT_BASE+$24)

BLT_ENDMASK1    		equ     (BLT_BASE+$28)
BLT_ENDMASK2    		equ     (BLT_BASE+$2A)
BLT_ENDMASK3    		equ     (BLT_BASE+$2C)

BLT_DST_XINC    		equ     (BLT_BASE+$2E)
BLT_DST_YINC    		equ     (BLT_BASE+$30)
BLT_DST_ADDR    		equ     (BLT_BASE+$32)

BLT_XCNT        		equ     (BLT_BASE+$36)
BLT_YCNT        		equ     (BLT_BASE+$38)

BLT_HOP        			equ     (BLT_BASE+$3A)
BLT_OP		 			equ     (BLT_BASE+$3B)
BLT_CTRL				equ     (BLT_BASE+$3C)
BLT_SKEW				equ     (BLT_BASE+$3D)

BLT_M_LINE_BUSY         equ  %00000111        ; mask for the Blitter line busy bit
BLT_F_LINE_BUSY         equ  %10000000        ; flag to set the Blitter line busy bit in shared (BLIT) mode
BLT_HOG_MODE            equ  %11000000        ; flag to set the Blitter line busy bit in exclusive (HOG) mode 

; Video base address
VIDEO_BASE_ADDR_LOW     equ $ffff820d
VIDEO_BASE_ADDR_MID     equ $ffff8203
VIDEO_BASE_ADDR_HIGH    equ $ffff8201


	include inc/tos.s

; Macros
; XBIOS Vsync wait
vsync_wait          macro
					move.w #37,-(sp)
					trap #14
					addq.l #2,sp
                    endm    

; XBIOS GetRez
; Return the current screen resolution in D0
get_rez				macro
					move.w #4,-(sp)
					trap #14
					addq.l #2,sp
					endm

; XBIOS Get Screen Base
; Return the screen memory address in D0
get_screen_base		macro
					move.w #2,-(sp)
					trap #14
					addq.l #2,sp
					endm

; XBIOS Set the screen memory address
set_screen_base	    macro
					move.w #-1, -(sp);  ; No change res
					pea \1 				; Set the physical screen address
					pea \1 				; Set the logical screen address
					move.w #5,-(sp)		; Set the function number to 3 (set screen base)
					trap #14			; Call XBIOS
					lea 12(sp), sp		; Clean the stack
					endm

; Check the keys pressed
check_keys			macro

					gemdos	Cconis,2		; Check if a key is pressed
					tst.l d0
					beq .\@no_key

					gemdos	Cnecin,2		; Read the key pressed

					cmp.b #27, d0			; Check if the key is ESC
					bne .\@any_key
.\@esc_key:
					; For performance reasons, we will positive and negative index values to avoid some operations
					move.l #(ROMCMD_START_ADDR + $8000), a0 ; Start address of the ROM3
					; SEND HEADER WITH MAGIC NUMBER
					move.w #CMD_BOOSTER, d7 	 ; Command 
					tst.b (a0, d7.w)             ; Command

					bra .\@no_key

.\@any_key:			bra boot_gem			; If any key is pressed, boot GEM

					; If we are here, no key was pressed
.\@no_key:

					endm

check_commands		macro
					move.l (LISTENER_ADDR), d6	; Store in the D6 register the remote command value
					cmp.l #REMOTE_RESET, d6		; Check if the command is a reset command
					beq .reset
					endm

	section

;Rom cartridge

	org ROM4_ADDR

	dc.l $abcdef42 					; magic number
first:
;	dc.l second
	dc.l 0
	dc.l $08000000 + pre_auto		; After GEMDOS init (before booting from disks)
	dc.l 0
	dc.w GEMDOS_TIME 				;time
	dc.w GEMDOS_DATE 				;date
	dc.l end_pre_auto - pre_auto
	dc.b "DEMO",0
    even

pre_auto:

; Get the resolution of the screen
.get_resolution:
	get_rez
	tst.w d0
	bne lowres_only

; Wait for the code to be copied to the framebuffers
	move.l #(50 * 5), d7 	; Approx 5 seconds maximum waiting
wait_code:
	vsync_wait

	subq #1, d7
	beq boot_gem

	move.w #$070, $FFFF8240.w 	; Set the index 0 color to black
	cmp.w  #$4E75, (COPYCODE_A_SRCADDR + COPYCODE_SIZE)
	bne.s wait_code
	move.w #$007, $FFFF8240.w 	; Set the index 0 color to black
	cmp.w  #$4E75, (COPYCODE_B_SRCADDR + COPYCODE_SIZE)
	bne.s wait_code

start_demo:
; Move the code below the screen memory
	lea (SCREEN_A_BASE_ADDR-COPIED_CODE_OFFSET), a2
	; Copy the code out of the ROM to avoid unstable behavior
    move.l #COPIED_CODE_SIZE, d6
    lea ROM4_ADDR, a1    ; a1 points to the start of the code in ROM
    lsr.w #2, d6
    subq #1, d6

.copy_rom_code:
    move.l (a1)+, (a2)+
    dbf d6, .copy_rom_code
	lea SCREEN_A_BASE_ADDR - COPIED_CODE_OFFSET + (start_rom_code - ROM4_ADDR), a3	; Save the screen memory address in A3
	jmp (a3)

start_rom_code:
    ; For performance reasons, we will positive and negative index values to avoid some operations
    move.l #(ROMCMD_START_ADDR + $8000), a0 ; Start address of the ROM3
    ; SEND HEADER WITH MAGIC NUMBER
    move.w #CMD_START_DEMO, d7 	 ; Command START_DEMO
    tst.b (a0, d7.w)             ; 

; Set colors
 	move.w #$000, $FF8240 	; Set the index 0 color to black
 	move.w #$311, $FF8242 	; Set the index 1 color
 	move.w #$511, $FF8244 	; Set the index 2 color
 	move.w #$711, $FF8246 	; Set the index 3 color
 	move.w #$131, $FF8248 	; Set the index 4 color
 	move.w #$331, $FF824A 	; Set the index 5 color
 	move.w #$531, $FF824C 	; Set the index 6 color
 	move.w #$731, $FF824E 	; Set the index 7 color
 	move.w #$113, $FF8250 	; Set the index 8 color
 	move.w #$133, $FF8252 	; Set the index 9 color
 	move.w #$333, $FF8254 	; Set the index 10 color
 	move.w #$751, $FF8256 	; Set the index 11 color
 	move.w #$171, $FF8258 	; Set the index 12 color
 	move.w #$555, $FF825A 	; Set the index 13 color
 	move.w #$733, $FF825C 	; Set the index 14 color
 	move.w #$777, $FF825E 	; Set the index 15 color

; Detect if we are a ST or STE
	move.l _p_cookies.w,d0      ; Check the cookie-jar to know what type of machine we are running on
	beq .loop_low_st      ; No cookie-jar, so it's a TOS <= 1.04
	movea.l d0,a0               ; Get the address of the cookie-jar
.loop_mch_cookie:
	move.l (a0)+,d0             ; The cookie jar value is zero, so old hardware again
	beq .loop_low_st
	cmp.l #'_MCH',d0            ; Is it the _MCH cookie?
	beq.s .found_mch_cookie         ; Yes, so we found the machine type
	addq.w #4,a0                ; No, so skip the cookie name
	bra.s .loop_mch_cookie      ; And try the next cookie
.found_mch_cookie:
	move.l	(a0)+,d0            ; Get the cookie value
; Check for MegaSTE or STE and if so, branch to the appropriate handler
    cmp.l #COOKIE_JAR_MEGASTE, d0
    beq .loop_low_ste
    cmp.l #COOKIE_JAR_STE, d0
    beq .loop_low_ste


.loop_low_st:

	vsync_wait

    ; For performance reasons, we will positive and negative index values to avoid some operations
    move.l #(ROMCMD_START_ADDR + $8000), a0 ; Start address of the ROM3
    ; SEND HEADER WITH MAGIC NUMBER
    move.w #CMD_VBLANK, d7 	     ; Command VBLANK
    tst.b (a0, d7.w)             ; 

 	move.w #$000, $FFFF8240.w 	; Set the index 0 color to black

	move.w sr, _dskbufp.w					; Save the status register

	ori.w #$0700, sr						; Disable interrupts

	tst.l $FA05FC
	beq.s .fb_b
.fb_a:
	jsr COPYCODE_A_ADDR
	bra.s .continue
.fb_b:
	jsr COPYCODE_B_ADDR

.continue:
	move.w _dskbufp.w, sr		; Restore the status register

 	move.w #$500, $FFFF8240.w 	; Set the index 0 color to red

; Check the different commands and the keyboard
	check_keys
	check_commands

	bra .loop_low_st	; Continue displaying framebuffers in Atari ST mode

.loop_low_ste:

	vsync_wait


 	move.w #$050, $FFFF8240.w 	; Set the index 0 color to green

    ; For performance reasons, we will positive and negative index values to avoid some operations
    move.l #(ROMCMD_START_ADDR + $8000), a0 ; Start address of the ROM3
    ; SEND HEADER WITH MAGIC NUMBER
    move.w #CMD_VBLANK, d7 	     ; Command VBLANK
    tst.b (a0, d7.w)             ; 

	move.w sr, _dskbufp.w					; Save the status register

	ori.w #$0700, sr						; Disable interrupts

    move.w  #2,BLT_SRC_XINC.w         ; +2 bytes per word inside line
    move.w  #2,BLT_DST_XINC.w
    clr.w  BLT_SRC_YINC.w             ; linear stream: next "line" starts
	clr.w  BLT_DST_YINC.w             ; immediately after previous (no extra stride)

    move.w  #$FFFF,BLT_ENDMASK1.w     ; fully open masks
    move.w  #$FFFF,BLT_ENDMASK2.w
    move.w  #$FFFF,BLT_ENDMASK3.w

    clr.b  BLT_SKEW.w                 ; no bit shift

    move.w  #16000,BLT_XCNT.w         ; words per line (640*2 = 1280 bytes)
    move.w  #1,BLT_YCNT.w             ; lines (25 * 1280 = 32000 bytes)

	move.b #$2, BLT_HOP.w 			  ; blitter HOP operation. Copy src to dest, 1:1 operation.
	move.b #$3, BLT_OP.w 			  ; blitter operation. Copy src to dest, replace copy.


	tst.l $FA05FC
	bne.s .fb_b_ste
.fb_a_ste:
    move.l  #SCREEN_A_BASE_ADDR,BLT_DST_ADDR.w     ; destination (even-aligned)
    move.l  #FRAMEBUFFER_A_ADDR,BLT_SRC_ADDR     ; source (even-aligned)
	move.b  #(SCREEN_B_BASE_ADDR >> 16), d0
	move.b  #((SCREEN_B_BASE_ADDR >> 8) & 8), d1
	move.b  #((SCREEN_B_BASE_ADDR >> 0) & 8), d2
	bra.s .continue_ste
.fb_b_ste:
    move.l  #SCREEN_B_BASE_ADDR,BLT_DST_ADDR.w     ; destination (even-aligned)
    move.l  #FRAMEBUFFER_B_ADDR,BLT_SRC_ADDR     ; source (even-aligned)
	move.b  #(SCREEN_A_BASE_ADDR >> 16), d0
	move.b  #((SCREEN_A_BASE_ADDR >> 8) & 8), d1
	move.b  #((SCREEN_A_BASE_ADDR >> 0) & 8), d2

.continue_ste:
	move.b  d0, VIDEO_BASE_ADDR_HIGH.w           ; put in high screen address byte
	move.b  d1, VIDEO_BASE_ADDR_MID.w           ; put in mid screen address byte
	move.b  d2, VIDEO_BASE_ADDR_LOW.w           ; put in low screen address byte (STe only)

 	move.w #$000, $FFFF8240.w 	; Set the index 0 color to black

    ; HOG mode
    move.b  #BLT_HOG_MODE,BLT_CTRL.w

	move.w _dskbufp.w, sr		; Restore the status register

 	move.w #$005, $FFFF8240.w 	; Set the index 0 color to blue

; Check the different commands and the keyboard
	check_keys
	check_commands

;	gemdos  Crawcin,8

	bra .loop_low_ste	; Continue displaying framebuffers in Atari STE mode

.reset:
    move.l #PRE_RESET_WAIT, d6
.wait_me:
    subq.l #1, d6           ; Decrement the outer loop
    bne.s .wait_me          ; Wait for the timeout

	clr.l $420.w			; Invalidate memory system variables
	clr.l $43A.w
	clr.l $51A.w
	move.l $4.w, a0			; Now we can safely jump to the reset vector
	jmp (a0)
	nop
.end_reset_code_in_stack:

lowres_only:
	print lowres_only_txt

boot_gem:
	; If we get here, continue loading GEM
    rts

lowres_only_txt: 
	dc.b "Demo only supports low res",$d,$a
	dc.b 0

	even


rom_function:
	; Place here your driver code
	rts


end_rom_code:
end_pre_auto:
	even

	dc.l 0