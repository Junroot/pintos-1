#define F (1 << 14)	//fixed point 1
#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))
// x and y denote fixed_point numbers in 17.14 format
// n is an integer

int int_to_fp(int n);	//integer -> fixed point
int fp_to_int_round(int x);	//FP -> integer(rounding)
int fp_to_int(int x);	//FP -> integer(truncating)
int add_fp(int x, int y);	// FP + FP
int add_mixed(int x, int n);	//FP + int
int sub_fp(int x, int y);	//FP - FP
int sub_mixed(int x, int n);	//FP - int
int mult_fp(int x, int y);	//FP * FP
int mult_mixed(int x, int n);	//FP * int
int div_fp(int x, int y);	//FP / FP
int div_mixed(int x, int n); //FP / int

int int_to_fp(int n)
{
	return n*F;
}

int fp_to_int_round(int x)
{
	if (x >= 0)	return (x+F/2)/F;
	else	return (x-F/2)/F;
}

int fp_to_int(int x)
{
	return x/F;
}

int add_fp(int x, int y)
{
	return x+y;
}

int add_mixed(int x, int n)
{
	return x+n*F;
}

int sub_fp(int x, int y)
{
	return x-y;
}

int sub_mixed(int x, int n)
{
	return x-n*F;
}

int mult_fp(int x, int y)
{
	return ((long long) x)*y/F;
}

int mult_mixed(int x, int n)
{
	return x*n;
}

int div_fp(int x, int y)
{
	return ((long long)x)*F/y;
}

int div_mixed(int x, int n)
{
	return x/n;
}
