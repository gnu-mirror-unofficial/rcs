SRCS=	ci.1l co.1l ident.1l rcs.1l rcsclean.1l rcsdiff.1l rcsfile.5l rcsfreeze.1l \
	rcsintro.1l rcsmerge.1l rlog.1l merge.1l
OBJS1=	ci.0 co.0 ident.0 rcs.0 rcsclean.0 rcsdiff.0 rcsfreeze.0 rcsintro.0 \
	rcsmerge.0 rlog.0 merge.0
OBJS5=	rcsfile.0
OBJS=	${OBJS1} ${OBJS5}
LINKS=
MANROFF = nroff -man


.SUFFIXES: .5l .1l .0

.5l.0:
	${MANROFF} $*.5l > $*.0
.1l.0:
	${MANROFF} $*.1l > $*.0

all: ${OBJS}

clean: FRC
	rm -f ${OBJS}

install: FRC
	for i in ${OBJS1}; do \
	    install -c -o bin -g bin -m 444 $$i ${DESTDIR}/usr/man/cat1; \
	done
	for i in ${OBJS5}; do \
	    install -c -o bin -g bin -m 444 $$i ${DESTDIR}/usr/man/cat5; \
	done

FRC:
