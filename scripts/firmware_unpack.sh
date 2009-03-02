#/bin/sh


if [ $# -ne 2 ]; then
	echo "Usage: $0 <file.bin> <output_directory>" >&2
	exit 1;
fi

file=$1
out_dir=$2

mkdir -p $out_dir

dd if=$file of=$out_dir/zImage bs=1M count=1 skip=1
dd if=$file of=$out_dir/ro.fs bs=1M count=6 skip=2
dd if=$file of=$out_dir/root.fs bs=1M count=44 skip=8
dd if=$file of=$out_dir/logo bs=1 count=240000 skip=$[1024*1024*52]
dd if=$file of=$out_dir/jffs2.fs bs=1M count=2 skip=53
