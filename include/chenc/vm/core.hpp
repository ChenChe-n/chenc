#pragma once

#include "chenc/core/type.hpp"

#include <bit>
#include <stdexcept>

namespace chenc::vm {

	// 指令集架构设计为小端序

	enum code_type_list : u16 {
		reg4s_imm01b = 0b000,
		reg3s_imm06b = 0b001,
		reg2s_imm11b = 0b010,
		reg1s_imm16b = 0b011,
		reg0s_imm21b = 0b100,
		// 除了低3位为类型保留 剩余29位都可自定义
		_0_ = 0b101, // 保留区域
		_1_ = 0b110, // 保留区域
		_2_ = 0b111, // 保留区域
	};

	template <u32 N>
	inline constexpr i64 sign_ext(u64 val) noexcept {
		static_assert(N > 0 && N < 64);
		return (i64)(val << (64 - N)) >> (64 - N);
	}

	struct opcode_t {
		// reg0s_imm21b  type[0,3) opcode[3,11)                                                 imm21b[11,32)
		// reg4s_imm01b  type[0,3) opcode[3,11) reg0[11,16) reg1[16,21) reg2[21,26) reg3[26,31) imm01b[31,32)
		// reg3s_imm06b  type[0,3) opcode[3,11) reg0[11,16) reg1[16,21) reg2[21,26)             imm06b[26,32)
		// reg2s_imm11b  type[0,3) opcode[3,11) reg0[11,16) reg1[16,21)                         imm11b[21,32)
		// reg1s_imm16b  type[0,3) opcode[3,11) reg0[11,16)                                     imm16b[16,32)
		// _0_
		// _1_
		// _2_

		u32 code_;

		inline constexpr void decode(u32 code) noexcept {
			code_ = code;
		}
		inline constexpr u16 opcode_type() const noexcept {
			return (code_ & 0x7FF);
		}
		inline constexpr u8 reg0() const noexcept {
			return ((code_ >> 11) & ((u64(1) << 5) - 1));
		}
		inline constexpr u8 reg1() const noexcept {
			return ((code_ >> 16) & ((u64(1) << 5) - 1));
		}
		inline constexpr u8 reg2() const noexcept {
			return ((code_ >> 21) & ((u64(1) << 5) - 1));
		}
		inline constexpr u8 reg3() const noexcept {
			return ((code_ >> 26) & ((u64(1) << 5) - 1));
		}
		inline constexpr u8 imm1() const noexcept {
			return ((code_ >> 31) & ((u64(1) << 1) - 1));
		}
		inline constexpr u8 imm6() const noexcept {
			return ((code_ >> 26) & ((u64(1) << 6) - 1));
		}
		inline constexpr u16 imm11() const noexcept {
			return ((code_ >> 21) & ((u64(1) << 11) - 1));
		}
		inline constexpr u16 imm16() const noexcept {
			return ((code_ >> 16) & ((u64(1) << 16) - 1));
		}
		inline constexpr u32 imm21() const noexcept {
			return ((code_ >> 11) & ((u64(1) << 21) - 1));
		}
	};
	struct reg_t {
		u64 reg_[32];
		enum reg_map {
			// 零寄存器
			zero = 0,
			// 临时寄存器
			t0 = 1,
			t1 = 2,
			t2 = 3,
			t3 = 4,
			t4 = 5,
			t5 = 6,
			t6 = 7,
			t7 = 8,
			// 通用寄存器
			// 调用函数前需将函数使用的寄存器保存到栈中
			// 由调用者保存和恢复
			x0 = 9,
			x1 = 10,
			x2 = 11,
			x3 = 12,
			x4 = 13,
			x5 = 14,
			x6 = 15,
			x7 = 16,
			x8 = 17,
			x9 = 18,
			x10 = 19,
			// 函数寄存器
			ret = 20, // 返回值寄存器
			a0 = 21,  // 函数参数寄存器
			a1 = 22,  // 函数参数寄存器
			a2 = 23,  // 函数参数寄存器
			a3 = 24,  // 函数参数寄存器
			a4 = 25,  // 函数参数寄存器
			a5 = 26,  // 函数参数寄存器
			a6 = 27,  // 函数参数寄存器
			// 专用寄存器
			sys = 28, // 系统基址寄存器
					  // 系统函数, 数据等的内存基址

			fp = 29, // 帧指针
			sp = 30, // 栈指针

			base = 31, // 基址寄存器
					   // text data 等通过它获取
		};
		u64 PC_; // 程序计数器
				 // 存储下一条指令的内存地址

		inline constexpr void store(u8 reg, u64 val) noexcept {
			if (reg != reg_t::zero) {
				reg_[reg] = val;
			}
		}
		inline constexpr u64 load(u8 reg) const noexcept {
			return reg_[reg];
		}
	};

	// (u) 零扩展
	// (s) 符号扩展
	// (f) 单精度浮点数
	// (d) 双精度浮点数
	// <regx> 寄存器读取
	// >regx> 寄存器写入

	namespace opcode_list {
		enum class reg0s_imm21b : u8 {
#if 1				   // 控制指令
			halt_r0i1, // 停止运行
			nop__r0i1, // 空指令
#endif
#if 1						  // 跳转
			jump_r0i1,		  // >pc< =           ((u)imm21b * 4)
			jump_offset_r0i1, // >pc< = (u)<pc> + ((s)imm21b * 4)
#endif
#if 1						   // 数值读取
			immu_load21b_r0i1, // >t0< = (u)immA21b
			imms_load21b_r0i1, // >t0< = (s)immA21b

			immu_load16b_or_shift_r0i1,	  // >t0< = (u)<t0> | (u)imm[0,16)b <<  (u)imm[16,21)b
			immu_load16b_or_shift32_r0i1, // >t0< = (u)<t0> | (u)imm[0,16)b << ((u)imm[16,21)b + 32)
#endif
#if 1					  // 函数调用
			syscall_r0i1, // syscall((u)imm21b)
						  //
			call_r0i1,	  // stack.push_8B(<pc>)
						  // stack.push_8B(<fp>)
						  // >fp< = (u)<sp>
						  // >pc< = (u)<pc> + (u)imm21b - 4
						  //
			return_r0i1,  // >sp< = (u)<fp>
						  // >fp< = (u)stack.pop_8B()
						  // >pc< = (u)stack.pop_8B()
#endif
		};

		enum class reg4s_imm01b : u8 {
#if 1						  // 内存读写指令
			loadu_1s_1B_r4i0, // >reg0< = (u)ram.load_1B((u)<reg1> * (u)<reg2> + (u)<reg3>)
			loadu_1s_2B_r4i0, // >reg0< = (u)ram.load_2B((u)<reg1> * (u)<reg2> + (u)<reg3>)
			loadu_1s_4B_r4i0, // >reg0< = (u)ram.load_4B((u)<reg1> * (u)<reg2> + (u)<reg3>)
			loadu_1s_8B_r4i0, // >reg0< = (u)ram.load_8B((u)<reg1> * (u)<reg2> + (u)<reg3>)
			loads_1s_1B_r4i0, // >reg0< = (s)ram.load_1B((u)<reg1> * (u)<reg2> + (u)<reg3>)
			loads_1s_2B_r4i0, // >reg0< = (s)ram.load_2B((u)<reg1> * (u)<reg2> + (u)<reg3>)
			loads_1s_4B_r4i0, // >reg0< = (s)ram.load_4B((u)<reg1> * (u)<reg2> + (u)<reg3>)
			loads_1s_8B_r4i0, // >reg0< = (s)ram.load_8B((u)<reg1> * (u)<reg2> + (u)<reg3>)

			store_1s_1B_r4i0, // ram.store_1B((u)<reg1> * (u)<reg2> + (u)<reg3>, (u)<reg0>)
			store_1s_2B_r4i0, // ram.store_2B((u)<reg1> * (u)<reg2> + (u)<reg3>, (u)<reg0>)
			store_1s_4B_r4i0, // ram.store_4B((u)<reg1> * (u)<reg2> + (u)<reg3>, (u)<reg0>)
			store_1s_8B_r4i0, // ram.store_8B((u)<reg1> * (u)<reg2> + (u)<reg3>, (u)<reg0>)

			loadu_2s_1B_r4i0, // >reg0< = (u)ram.load_1B((u)<reg2> + (u)<reg3>); >reg1< = (u)ram.load_1B((u)<reg2> + (u)<reg3> + 1)
			loadu_2s_2B_r4i0, // >reg0< = (u)ram.load_2B((u)<reg2> + (u)<reg3>); >reg1< = (u)ram.load_2B((u)<reg2> + (u)<reg3> + 2)
			loadu_2s_4B_r4i0, // >reg0< = (u)ram.load_4B((u)<reg2> + (u)<reg3>); >reg1< = (u)ram.load_4B((u)<reg2> + (u)<reg3> + 4)
			loadu_2s_8B_r4i0, // >reg0< = (u)ram.load_8B((u)<reg2> + (u)<reg3>); >reg1< = (u)ram.load_8B((u)<reg2> + (u)<reg3> + 8)
			loads_2s_1B_r4i0, // >reg0< = (s)ram.load_1B((u)<reg2> + (u)<reg3>); >reg1< = (s)ram.load_1B((u)<reg2> + (u)<reg3> + 1)
			loads_2s_2B_r4i0, // >reg0< = (s)ram.load_2B((u)<reg2> + (u)<reg3>); >reg1< = (s)ram.load_2B((u)<reg2> + (u)<reg3> + 2)
			loads_2s_4B_r4i0, // >reg0< = (s)ram.load_4B((u)<reg2> + (u)<reg3>); >reg1< = (s)ram.load_4B((u)<reg2> + (u)<reg3> + 4)
			loads_2s_8B_r4i0, // >reg0< = (s)ram.load_8B((u)<reg2> + (u)<reg3>); >reg1< = (s)ram.load_8B((u)<reg2> + (u)<reg3> + 8)

			store_2s_1B_r4i0, // ram.store_1B((u)<reg2> + (u)<reg3>, (u)<reg0>); ram.store_1B((u)<reg2> + (u)<reg3> + 1, (u)<reg1>)
			store_2s_2B_r4i0, // ram.store_2B((u)<reg2> + (u)<reg3>, (u)<reg0>); ram.store_2B((u)<reg2> + (u)<reg3> + 2, (u)<reg1>)
			store_2s_4B_r4i0, // ram.store_4B((u)<reg2> + (u)<reg3>, (u)<reg0>); ram.store_4B((u)<reg2> + (u)<reg3> + 4, (u)<reg1>)
			store_2s_8B_r4i0, // ram.store_8B((u)<reg2> + (u)<reg3>, (u)<reg0>); ram.store_8B((u)<reg2> + (u)<reg3> + 8, (u)<reg1>)

			loadu_3s_1B_r4i0, // >reg0< = (u)ram.load_1B((u)<reg3>); >reg1< = (u)ram.load_1B((u)<reg3> + 1);  >reg2< = (u)ram.load_1B((u)<reg3> + 2 );
			loadu_3s_2B_r4i0, // >reg0< = (u)ram.load_2B((u)<reg3>); >reg1< = (u)ram.load_2B((u)<reg3> + 2);  >reg2< = (u)ram.load_2B((u)<reg3> + 4 );
			loadu_3s_4B_r4i0, // >reg0< = (u)ram.load_4B((u)<reg3>); >reg1< = (u)ram.load_4B((u)<reg3> + 4);  >reg2< = (u)ram.load_4B((u)<reg3> + 8 );
			loadu_3s_8B_r4i0, // >reg0< = (u)ram.load_8B((u)<reg3>); >reg1< = (u)ram.load_8B((u)<reg3> + 8);  >reg2< = (u)ram.load_8B((u)<reg3> + 16);
			loads_3s_1B_r4i0, // >reg0< = (s)ram.load_1B((u)<reg3>); >reg1< = (s)ram.load_1B((u)<reg3> + 1);  >reg2< = (s)ram.load_1B((u)<reg3> + 2 );
			loads_3s_2B_r4i0, // >reg0< = (s)ram.load_2B((u)<reg3>); >reg1< = (s)ram.load_2B((u)<reg3> + 2);  >reg2< = (s)ram.load_2B((u)<reg3> + 4 );
			loads_3s_4B_r4i0, // >reg0< = (s)ram.load_4B((u)<reg3>); >reg1< = (s)ram.load_4B((u)<reg3> + 4);  >reg2< = (s)ram.load_4B((u)<reg3> + 8 );
			loads_3s_8B_r4i0, // >reg0< = (s)ram.load_8B((u)<reg3>); >reg1< = (s)ram.load_8B((u)<reg3> + 8);  >reg2< = (s)ram.load_8B((u)<reg3> + 16);

			store_3s_1B_r4i0, // ram.store_1B((u)<<reg3>, (u)<reg0>); ram.store_1B((u)<<reg3> + 1, (u)<reg1>); ram.store_1B((u)<<reg3> + 2 , (u)<reg2>);
			store_3s_2B_r4i0, // ram.store_2B((u)<<reg3>, (u)<reg0>); ram.store_2B((u)<<reg3> + 2, (u)<reg1>); ram.store_2B((u)<<reg3> + 4 , (u)<reg2>);
			store_3s_4B_r4i0, // ram.store_4B((u)<<reg3>, (u)<reg0>); ram.store_4B((u)<<reg3> + 4, (u)<reg1>); ram.store_4B((u)<<reg3> + 8 , (u)<reg2>);
			store_3s_8B_r4i0, // ram.store_8B((u)<<reg3>, (u)<reg0>); ram.store_8B((u)<<reg3> + 8, (u)<reg1>); ram.store_8B((u)<<reg3> + 16, (u)<reg2>);
#endif
#if 1						 // 栈指令
			push_1s_1B_r4i0, // stack.push_1B(<reg0>);
			push_1s_2B_r4i0, // stack.push_2B(<reg0>);
			push_1s_4B_r4i0, // stack.push_4B(<reg0>);
			push_1s_8B_r4i0, // stack.push_8B(<reg0>);
			pop__1s_1B_r4i0, // >reg0< = stack.pop_1B();
			pop__1s_2B_r4i0, // >reg0< = stack.pop_2B();
			pop__1s_4B_r4i0, // >reg0< = stack.pop_4B();
			pop__1s_8B_r4i0, // >reg0< = stack.pop_8B();

			push_2s_1B_r4i0, // stack.push_1B(<reg0>);   stack.push_1B(<reg1>);
			push_2s_2B_r4i0, // stack.push_2B(<reg0>);   stack.push_2B(<reg1>);
			push_2s_4B_r4i0, // stack.push_4B(<reg0>);   stack.push_4B(<reg1>);
			push_2s_8B_r4i0, // stack.push_8B(<reg0>);   stack.push_8B(<reg1>);
			pop__2s_1B_r4i0, // >reg0< = stack.pop_1B(); >reg1< = stack.pop_1B();
			pop__2s_2B_r4i0, // >reg0< = stack.pop_2B(); >reg1< = stack.pop_2B();
			pop__2s_4B_r4i0, // >reg0< = stack.pop_4B(); >reg1< = stack.pop_4B();
			pop__2s_8B_r4i0, // >reg0< = stack.pop_8B(); >reg1< = stack.pop_8B();

			push_3s_1B_r4i0, // stack.push_1B(<reg0>);   stack.push_1B(<reg1>);   stack.push_1B(<reg2>);
			push_3s_2B_r4i0, // stack.push_2B(<reg0>);   stack.push_2B(<reg1>);   stack.push_2B(<reg2>);
			push_3s_4B_r4i0, // stack.push_4B(<reg0>);   stack.push_4B(<reg1>);   stack.push_4B(<reg2>);
			push_3s_8B_r4i0, // stack.push_8B(<reg0>);   stack.push_8B(<reg1>);   stack.push_8B(<reg2>);
			pop__3s_1B_r4i0, // >reg0< = stack.pop_1B(); >reg1< = stack.pop_1B(); >reg2< = stack.pop_1B();
			pop__3s_2B_r4i0, // >reg0< = stack.pop_2B(); >reg1< = stack.pop_2B(); >reg2< = stack.pop_2B();
			pop__3s_4B_r4i0, // >reg0< = stack.pop_4B(); >reg1< = stack.pop_4B(); >reg2< = stack.pop_4B();
			pop__3s_8B_r4i0, // >reg0< = stack.pop_8B(); >reg1< = stack.pop_8B(); >reg2< = stack.pop_8B();

			push_4s_1B_r4i0, // stack.push_1B(<reg0>);   stack.push_1B(<reg1>);   stack.push_1B(<reg2>);   stack.push_1B(<reg3>)
			push_4s_2B_r4i0, // stack.push_2B(<reg0>);   stack.push_2B(<reg1>);   stack.push_2B(<reg2>);   stack.push_2B(<reg3>)
			push_4s_4B_r4i0, // stack.push_4B(<reg0>);   stack.push_4B(<reg1>);   stack.push_4B(<reg2>);   stack.push_4B(<reg3>)
			push_4s_8B_r4i0, // stack.push_8B(<reg0>);   stack.push_8B(<reg1>);   stack.push_8B(<reg2>);   stack.push_8B(<reg3>)
			pop__4s_1B_r4i0, // >reg0< = stack.pop_1B(); >reg1< = stack.pop_1B(); >reg2< = stack.pop_1B(); >reg3< = stack.pop_1B()
			pop__4s_2B_r4i0, // >reg0< = stack.pop_2B(); >reg1< = stack.pop_2B(); >reg2< = stack.pop_2B(); >reg3< = stack.pop_2B()
			pop__4s_4B_r4i0, // >reg0< = stack.pop_4B(); >reg1< = stack.pop_4B(); >reg2< = stack.pop_4B(); >reg3< = stack.pop_4B()
			pop__4s_8B_r4i0, // >reg0< = stack.pop_8B(); >reg1< = stack.pop_8B(); >reg2< = stack.pop_8B(); >reg3< = stack.pop_8B()
#endif
#if 1					  // 浮点指令
			ftod_1s_r4i0, // >reg0< = (d)(f)<reg1>;
			dtof_1s_r4i0, // >reg0< = (f)(d)<reg1>;
			itod_1s_r4i0, // >reg0< = (d)(s)<reg1>;
			dtoi_1s_r4i0, // >reg0< = (s)(d)<reg1>;
			itof_1s_r4i0, // >reg0< = (f)(s)<reg1>;
			ftoi_1s_r4i0, // >reg0< = (s)(f)<reg1>;

			ftod_2s_r4i0, // >reg0< = (d)(f)<reg1>; >reg2< = (d)(f)<reg3>
			dtof_2s_r4i0, // >reg0< = (f)(d)<reg1>; >reg2< = (f)(d)<reg3>
			itod_2s_r4i0, // >reg0< = (d)(s)<reg1>; >reg2< = (d)(s)<reg3>
			dtoi_2s_r4i0, // >reg0< = (s)(d)<reg1>; >reg2< = (s)(d)<reg3>
			itof_2s_r4i0, // >reg0< = (f)(s)<reg1>; >reg2< = (f)(s)<reg3>
			ftoi_2s_r4i0, // >reg0< = (s)(f)<reg1>; >reg2< = (s)(f)<reg3>
#endif
		};

		enum class reg3s_imm06b : u8 {
#if 1				   // 算数指令
			addu_r3i1, // >reg0< = (u)<reg1> +  (u)<reg2>
			adds_r3i1, // >reg0< = (s)<reg1> +  (s)<reg2>
			subu_r3i1, // >reg0< = (u)<reg1> -  (u)<reg2>
			subs_r3i1, // >reg0< = (s)<reg1> -  (s)<reg2>
			mulu_r3i1, // >reg0< = (u)<reg1> *  (u)<reg2>
			muls_r3i1, // >reg0< = (s)<reg1> *  (s)<reg2>
			divu_r3i1, // >reg0< = (u)<reg1> /  (u)<reg2>
			divs_r3i1, // >reg0< = (s)<reg1> /  (s)<reg2>
			modu_r3i1, // >reg0< = (u)<reg1> %  (u)<reg2>
			mods_r3i1, // >reg0< = (s)<reg1> %  (s)<reg2>

			shlu_r3i1, // >reg0< = (u)<reg1> << (u)<reg2>
			shls_r3i1, // >reg0< = (s)<reg1> << (s)<reg2>
			shru_r3i1, // >reg0< = (u)<reg1> >> (u)<reg2>
			shrs_r3i1, // >reg0< = (s)<reg1> >> (s)<reg2>
			oru__r3i1, // >reg0< = (u)<reg1> |  (u)<reg2>
			ors__r3i1, // >reg0< = (u)<reg1> |  (s)<reg2>
			andu_r3i1, // >reg0< = (u)<reg1> &  (u)<reg2>
			ands_r3i1, // >reg0< = (u)<reg1> &  (s)<reg2>
			xoru_r3i1, // >reg0< = (u)<reg1> ^  (u)<reg2>
			xors_r3i1, // >reg0< = (u)<reg1> ^  (s)<reg2>
#endif
#if 1				   // 浮点指令
			addf_r3i1, // >reg0< = (f)<reg1> +  (f)<reg2>
			addd_r3i1, // >reg0< = (d)<reg1> +  (d)<reg2>
			subf_r3i1, // >reg0< = (f)<reg1> -  (f)<reg2>
			subd_r3i1, // >reg0< = (d)<reg1> -  (d)<reg2>
			mulf_r3i1, // >reg0< = (f)<reg1> *  (f)<reg2>
			muld_r3i1, // >reg0< = (d)<reg1> *  (d)<reg2>
			divf_r3i1, // >reg0< = (f)<reg1> /  (f)<reg2>
			divd_r3i1, // >reg0< = (d)<reg1> /  (d)<reg2>
#endif
#if 1						// 比较指令
			equ__zero_r3i1, // >reg0< = ((u)<reg1> == (u)<reg2>) ? (u)imm6b : 0
			eqs__zero_r3i1, // >reg0< = ((s)<reg1> == (s)<reg2>) ? (s)imm6b : 0
			nequ_zero_r3i1, // >reg0< = ((u)<reg1> != (u)<reg2>) ? (u)imm6b : 0
			neqs_zero_r3i1, // >reg0< = ((s)<reg1> != (s)<reg2>) ? (s)imm6b : 0
			ltu__zero_r3i1, // >reg0< = ((u)<reg1> <  (u)<reg2>) ? (u)imm6b : 0
			lts__zero_r3i1, // >reg0< = ((s)<reg1> <  (s)<reg2>) ? (s)imm6b : 0
			lteu_zero_r3i1, // >reg0< = ((u)<reg1> <= (u)<reg2>) ? (u)imm6b : 0
			ltes_zero_r3i1, // >reg0< = ((s)<reg1> <= (s)<reg2>) ? (s)imm6b : 0
			gtu__zero_r3i1, // >reg0< = ((u)<reg1> >  (u)<reg2>) ? (u)imm6b : 0
			gts__zero_r3i1, // >reg0< = ((s)<reg1> >  (s)<reg2>) ? (s)imm6b : 0
			gteu_zero_r3i1, // >reg0< = ((u)<reg1> >= (u)<reg2>) ? (u)imm6b : 0
			gtes_zero_r3i1, // >reg0< = ((s)<reg1> >= (s)<reg2>) ? (s)imm6b : 0
#endif
		};

		enum class reg2s_imm11b : u8 {
#if 1							   // 跳转
			jump_equ__offset_r2i1, // >pc< = (u)<reg0> == (u)<reg1> ? (u)<pc> + ((s)imm11b * 4) : (u)<pc>
			jump_nequ_offset_r2i1, // >pc< = (u)<reg0> != (u)<reg1> ? (u)<pc> + ((s)imm11b * 4) : (u)<pc>
			jump_ltu__offset_r2i1, // >pc< = (u)<reg0> <  (u)<reg1> ? (u)<pc> + ((s)imm11b * 4) : (u)<pc>
			jump_lts__offset_r2i1, // >pc< = (s)<reg0> <  (s)<reg1> ? (u)<pc> + ((s)imm11b * 4) : (u)<pc>
			jump_lteu_offset_r2i1, // >pc< = (u)<reg0> <= (u)<reg1> ? (u)<pc> + ((s)imm11b * 4) : (u)<pc>
			jump_ltes_offset_r2i1, // >pc< = (s)<reg0> <= (s)<reg1> ? (u)<pc> + ((s)imm11b * 4) : (u)<pc>
#endif
#if 1						  // 内存读写指令
			loadu_1s_1B_r2i1, // >reg0< = (u)ram.load_1B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b)
			loadu_1s_2B_r2i1, // >reg0< = (u)ram.load_2B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b)
			loadu_1s_4B_r2i1, // >reg0< = (u)ram.load_4B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b)
			loadu_1s_8B_r2i1, // >reg0< = (u)ram.load_8B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b)
			loads_1s_1B_r2i1, // >reg0< = (s)ram.load_1B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b)
			loads_1s_2B_r2i1, // >reg0< = (s)ram.load_2B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b)
			loads_1s_4B_r2i1, // >reg0< = (s)ram.load_4B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b)
			loads_1s_8B_r2i1, // >reg0< = (s)ram.load_8B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b)

			store_1s_1B_r2i1, // ram.store_1B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b, (u)<reg0>)
			store_1s_2B_r2i1, // ram.store_2B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b, (u)<reg0>)
			store_1s_4B_r2i1, // ram.store_4B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b, (u)<reg0>)
			store_1s_8B_r2i1, // ram.store_8B((u)<reg1> * (u)imm[0,6)b + (u)imm[6,11)b, (u)<reg0>)
#endif
#if 1				   // 算数指令
			addu_r2i1, // >reg0< = (u)<reg1> +  (u)imm11b
			adds_r2i1, // >reg0< = (s)<reg1> +  (s)imm11b
			subu_r2i1, // >reg0< = (u)<reg1> -  (u)imm11b
			subs_r2i1, // >reg0< = (s)<reg1> -  (s)imm11b
			mulu_r2i1, // >reg0< = (u)<reg1> *  (u)imm11b
			muls_r2i1, // >reg0< = (s)<reg1> *  (s)imm11b
			divu_r2i1, // >reg0< = (u)<reg1> /  (u)imm11b
			divs_r2i1, // >reg0< = (s)<reg1> /  (s)imm11b
			modu_r2i1, // >reg0< = (u)<reg1> %  (u)imm11b
			mods_r2i1, // >reg0< = (s)<reg1> %  (s)imm11b

			shlu_r2i1, // >reg0< = (u)<reg1> << (u)imm11b
			shls_r2i1, // >reg0< = (s)<reg1> << (s)imm11b
			shru_r2i1, // >reg0< = (u)<reg1> >> (u)imm11b
			shrs_r2i1, // >reg0< = (s)<reg1> >> (s)imm11b
			oru__r2i1, // >reg0< = (u)<reg1> |  (u)imm11b
			ors__r2i1, // >reg0< = (u)<reg1> |  (s)imm11b
			andu_r2i1, // >reg0< = (u)<reg1> &  (u)imm11b
			ands_r2i1, // >reg0< = (u)<reg1> &  (s)imm11b
			xoru_r2i1, // >reg0< = (u)<reg1> ^  (u)imm11b
			xors_r2i1, // >reg0< = (u)<reg1> ^  (s)imm11b
#endif
#if 1						// 比较指令
			equ__zero_r2i1, // >reg0< = ((u)<reg1> == 0) ? (u)imm11b : 0
			eqs__zero_r2i1, // >reg0< = ((s)<reg1> == 0) ? (s)imm11b : 0
			nequ_zero_r2i1, // >reg0< = ((u)<reg1> != 0) ? (u)imm11b : 0
			neqs_zero_r2i1, // >reg0< = ((s)<reg1> != 0) ? (s)imm11b : 0
			ltu__zero_r2i1, // >reg0< = ((u)<reg1> <  0) ? (u)imm11b : 0
			lts__zero_r2i1, // >reg0< = ((s)<reg1> <  0) ? (s)imm11b : 0
			lteu_zero_r2i1, // >reg0< = ((u)<reg1> <= 0) ? (u)imm11b : 0
			ltes_zero_r2i1, // >reg0< = ((s)<reg1> <= 0) ? (s)imm11b : 0
			gtu__zero_r2i1, // >reg0< = ((u)<reg1> >  0) ? (u)imm11b : 0
			gts__zero_r2i1, // >reg0< = ((s)<reg1> >  0) ? (s)imm11b : 0
			gteu_zero_r2i1, // >reg0< = ((u)<reg1> >= 0) ? (u)imm11b : 0
			gtes_zero_r2i1, // >reg0< = ((s)<reg1> >= 0) ? (s)imm11b : 0
#endif
		};

		enum class reg1s_imm16b : u8 {
#if 1								   // 数值读取
			immu_load16b_r1i1,		   // >reg0< = (u)imm16b << (0 * 8)
			imms_load16b_r1i1,		   // >reg0< = (s)imm16b << (0 * 8)
			immu_load16b_shift16_r1i1, // >reg0< = (u)imm16b << (2 * 8)
			imms_load16b_shift16_r1i1, // >reg0< = (s)imm16b << (2 * 8)
			immu_load16b_shift32_r1i1, // >reg0< = (u)imm16b << (4 * 8)
			imms_load16b_shift32_r1i1, // >reg0< = (s)imm16b << (4 * 8)
			immu_load16b_shift48_r1i1, // >reg0< = (u)imm16b << (6 * 8)
			imms_load16b_shift48_r1i1, // >reg0< = (s)imm16b << (6 * 8)

			immu_load16b_or_r1i1,		  // >reg0< = (u)<reg0> | (u)imm16b << (0 * 8)
			immu_load16b_or_shift16_r1i1, // >reg0< = (u)<reg0> | (u)imm16b << (2 * 8)
			immu_load16b_or_shift32_r1i1, // >reg0< = (u)<reg0> | (u)imm16b << (4 * 8)
			immu_load16b_or_shift48_r1i1, // >reg0< = (u)<reg0> | (u)imm16b << (6 * 8)

#endif
#if 1				   // 算数指令
			addu_r1i1, // >reg0< = (u)<reg0> +  (u)imm16b
			adds_r1i1, // >reg0< = (s)<reg0> +  (s)imm16b
			subu_r1i1, // >reg0< = (u)<reg0> -  (u)imm16b
			subs_r1i1, // >reg0< = (s)<reg0> -  (s)imm16b
			mulu_r1i1, // >reg0< = (u)<reg0> *  (u)imm16b
			muls_r1i1, // >reg0< = (s)<reg0> *  (s)imm16b
			divu_r1i1, // >reg0< = (u)<reg0> /  (u)imm16b
			divs_r1i1, // >reg0< = (s)<reg0> /  (s)imm16b
			modu_r1i1, // >reg0< = (u)<reg0> %  (u)imm16b
			mods_r1i1, // >reg0< = (s)<reg0> %  (s)imm16b

			shlu_r1i1, // >reg0< = (u)<reg0> << (u)imm16b
			shls_r1i1, // >reg0< = (s)<reg0> << (s)imm16b
			shru_r1i1, // >reg0< = (u)<reg0> >> (u)imm16b
			shrs_r1i1, // >reg0< = (s)<reg0> >> (s)imm16b
			oru__r1i1, // >reg0< = (u)<reg0> |  (u)imm16b
			ors__r1i1, // >reg0< = (u)<reg0> |  (s)imm16b
			andu_r1i1, // >reg0< = (u)<reg0> &  (u)imm16b
			ands_r1i1, // >reg0< = (u)<reg0> &  (s)imm16b
			xoru_r1i1, // >reg0< = (u)<reg0> ^  (u)imm16b
			xors_r1i1, // >reg0< = (u)<reg0> ^  (s)imm16b
#endif
#if 1						  // 跳转
			jump_r1i1,		  // >pc< =           (u)<reg0> + ((u)imm16b * 4)
			jump_offset_r1i1, // >pc< = (u)<pc> + (u)<reg0> + ((s)imm16b * 4)

			jump_equ__zero_offset_r1i1, // >pc< = (u)<reg0> == 0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
			jump_nequ_zero_offset_r1i1, // >pc< = (u)<reg0> != 0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
			jump_ltu__zero_offset_r1i1, // >pc< = (u)<reg0> <  0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
			jump_lts__zero_offset_r1i1, // >pc< = (s)<reg0> <  0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
			jump_lteu_zero_offset_r1i1, // >pc< = (u)<reg0> <= 0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
			jump_ltes_zero_offset_r1i1, // >pc< = (s)<reg0> <= 0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
			jump_gtu__zero_offset_r1i1, // >pc< = (u)<reg0> >  0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
			jump_gts__zero_offset_r1i1, // >pc< = (s)<reg0> >  0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
			jump_gteu_zero_offset_r1i1, // >pc< = (u)<reg0> >= 0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
			jump_gtes_zero_offset_r1i1, // >pc< = (s)<reg0> >= 0 ? (u)<pc> + ((s)imm16b * 4) : (u)<pc>
#endif
#if 1					  // 函数调用
			syscall_r1i1, // syscall((u)imm16b)
						  //
			call_r1i1,	  // stack.push_8B(<pc>)
						  // stack.push_8B(<fp>)
						  // >fp< = (u)<sp>
						  // >pc< = (u)<reg0> - 4
						  //
			return_r1i1,  // >sp< = (u)<fp>
						  // >fp< = (u)stack.pop_8B()
						  // >pc< = (u)stack.pop_8B()
#endif
		};

	} // namespace opcode_list

} // namespace chenc::vm