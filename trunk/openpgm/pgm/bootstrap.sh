#!/bin/sh

# ----------------------------------------------------------------------
#  Globals
# ----------------------------------------------------------------------
DIE=0
ELINES=3


# ----------------------------------------------------------------------
#  Color settings
# ----------------------------------------------------------------------
B=`tput bold 2>/dev/null`
N=`tput sgr0 2>/dev/null`

ERROR="${B}** エラー **${N}"
WARNING="${B}* 警告 *${N}"


# ----------------------------------------------------------------------
#  Functions
# ----------------------------------------------------------------------
CHECK() {
	for PROG in $*
	do
		VERSION=`$PROG --version 2>/dev/null | head -1 | sed -e "s/$PROG //"`
		if test \! -z "$VERSION"
		then
			echo "${PROG} ${VERSION} を使用する"
			USE_PROG=$PROG
			break
		fi
	done

	if test -z "VERSION"
	then
		echo
		echo "${ERROR} : お使いのシステムにインストールされている「${B}${PROG}${N}」を持っている必要があります。"
		echo
		DIE=1
	fi
}


CHECK_WARN() {
	for PROG in $*
	do
		VERSION=`$PROG --version 2>/dev/null | head -1 | sed -e "s/$PROG //"`
		if test \! -z "$VERSION"
		then
			echo "${PROG} ${VERSION} を使用する"
			USE_PROG=$PROG
			break
		fi
	done

	if test -z "VERSION"
	then
		echo
		echo "${WARNING} : お使いのシステムにインストールされている「${B}${PROG}${N}」が必要な場合があります。"
		echo
	fi
}


RUN() {
	PROG=$1; shift
	ARGS=$*

	echo "${B}${PROG}${N} を実行している..."
	$PROG $ARGS 2>> bootstrap.log
	if test $? -ne 0
	then
		echo
		echo "${ERROR}"
		tail -$ELINES bootstrap.log
		echo
		echo "ブートストラップスクリプト中止（詳細については、bootstrap.log を参照してください）..."
		exit 1
	fi
}


#
#  Check availability of build tools
#
echo
echo "${B}ビルド環境をチェックする${N}..."

CHECK libtoolize glibtoolize; LIBTOOLIZE=$USE_PROG
CHECK aclocal
CHECK autoheader
CHECK automake
CHECK autoconf

if test "$DIE" -eq 1
then
	echo
	echo "ブートストラップスクリプト中止..."
	exit 1
fi


#
#  Generate the build scripts
#
> bootstrap.log

echo
echo "${B}ビルドスクリプトを生成する${N}..."

RUN $LIBTOOLIZE --force --copy
RUN aclocal
RUN autoheader
RUN automake --copy --add-missing
RUN autoconf

echo
echo "ブートストラップスクリプトが正常に完了しました。"

exit 0
