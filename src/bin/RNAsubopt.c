/* Last changed Time-stamp: <2008-12-03 16:38:01 ivo> */
/*
                Ineractive access to suboptimal folding

                           c Ivo L Hofacker
                          Vienna RNA package
*/
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include "ViennaRNA/part_func.h"
#include "ViennaRNA/fold.h"
#include "ViennaRNA/cofold.h"
#include "ViennaRNA/fold_vars.h"
#include "ViennaRNA/utils.h"
#include "ViennaRNA/read_epars.h"
#include "ViennaRNA/subopt.h"
#include "ViennaRNA/params.h"
#include "ViennaRNA/constraints.h"
#include "ViennaRNA/file_formats.h"
#include "RNAsubopt_cmdl.h"

/*@unused@*/
static char UNUSED rcsid[] = "$Id: RNAsubopt.c,v 1.20 2008/12/03 16:55:44 ivo Exp $";

PRIVATE char *tokenize(char *line);
PRIVATE void putoutzuker(SOLUTION* zukersolution);

int main(int argc, char *argv[]){
  struct          RNAsubopt_args_info args_info;
  unsigned int    input_type;
  unsigned int    rec_type, read_opt;
  char            fname[FILENAME_MAX_LENGTH], *c, *input_string, *rec_sequence, *rec_id, **rec_rest, *orig_sequence;
  char            *cstruc, *structure, *ParamFile, *ns_bases;
  int             i, length, l, cl, sym, istty;
  double          deltaf, deltap, betaScale;
  int             delta, n_back, noconv, circular, dos, zuker, gquad;
  model_detailsT  md;

  do_backtrack  = 1;
  dangles       = 2;
  betaScale     = 1.;
  gquad         = 0;
  delta         = 100;
  deltap = n_back = noconv = circular = dos = zuker = 0;
  rec_type      = read_opt = 0;
  rec_id        = rec_sequence = orig_sequence = NULL;
  rec_rest      = NULL;
  input_string  = c = cstruc = structure = ParamFile = ns_bases = NULL;

  set_model_details(&md);

  /* switch on unique multibranch loop decomposition */
  md.uniq_ML = 1;

  /*
  #############################################
  # check the command line parameters
  #############################################
  */
  if(RNAsubopt_cmdline_parser (argc, argv, &args_info) != 0) exit(1);
  /* temperature */
  if(args_info.temp_given)        md.temperature = temperature = args_info.temp_arg;
  /* structure constraint */
  if(args_info.constraint_given)  fold_constrained=1;
  /* do not take special tetra loop energies into account */
  if(args_info.noTetra_given)     md.special_hp = tetra_loop = 0;
  /* set dangle model */
  if(args_info.dangles_given){
    if((args_info.dangles_arg < 0) || (args_info.dangles_arg > 3))
      warn_user("required dangle model not implemented, falling back to default dangles=2");
    else
      md.dangles = dangles = args_info.dangles_arg;
  }
  /* do not allow weak pairs */
  if(args_info.noLP_given)        md.noLP = noLonelyPairs = 1;
  /* do not allow wobble pairs (GU) */
  if(args_info.noGU_given)        md.noGU = noGU = 1;
  /* do not allow weak closing pairs (AU,GU) */
  if(args_info.noClosingGU_given) md.noGUclosure = no_closingGU = 1;
  /* gquadruplex support */
  if(args_info.gquad_given)       md.gquad = gquad = 1;
  /* enforce canonical base pairs in any case? */
  if(args_info.canonicalBPonly_given) md.canonicalBPonly = canonicalBPonly = 1;
  /* do not convert DNA nucleotide "T" to appropriate RNA "U" */
  if(args_info.noconv_given)      noconv = 1;
  /* take another energy parameter set */
  if(args_info.paramFile_given)   ParamFile = strdup(args_info.paramFile_arg);
  /* Allow other pairs in addition to the usual AU,GC,and GU pairs */
  if(args_info.nsp_given)         ns_bases = strdup(args_info.nsp_arg);
  /* energy range */
  if(args_info.deltaEnergy_given) delta = (int) (0.1+args_info.deltaEnergy_arg*100);
  /* energy range after post evaluation */
  if(args_info.deltaEnergyPost_given) deltap = args_info.deltaEnergyPost_arg;
  /* sorted output */
  if(args_info.sorted_given)      subopt_sorted = 1;
  /* assume RNA sequence to be circular */
  if(args_info.circ_given)        md.circ = circular = 1;
  /* stochastic backtracking */
  if(args_info.stochBT_given){
    n_back = args_info.stochBT_arg;
    init_rand();
  }
  if(args_info.betaScale_given)   md.betaScale = betaScale = args_info.betaScale_arg;
  /* density of states */
  if(args_info.dos_given){
    dos = 1;
    print_energy = -999999;
  }
  /* logarithmic multiloop energies */
  if(args_info.logML_given) md.logML = logML = 1;
  /* zuker subopts */
  if(args_info.zuker_given) zuker = 1;

  if(zuker){
    if(circular){
      warn_user("Sorry, zuker subopts not yet implemented for circfold");
      RNAsubopt_cmdline_parser_print_help();
      exit(1);
    }
    else if(n_back>0){
      warn_user("Can't do zuker subopts and stochastic subopts at the same time");
      RNAsubopt_cmdline_parser_print_help();
      exit(1);
    }
    else if(gquad){
      warn_user("G-quadruplex support for Zuker subopts not implemented yet");
      RNAsubopt_cmdline_parser_print_help();
      exit(1);
    }
  }

  if(gquad && (n_back > 0)){
    warn_user("G-quadruplex support for stochastic backtracking not implemented yet");
    RNAsubopt_cmdline_parser_print_help();
    exit(1);
  }

  /* free allocated memory of command line data structure */
  RNAsubopt_cmdline_parser_free(&args_info);

  /*
  #############################################
  # begin initializing
  #############################################
  */

  if (ParamFile != NULL) read_parameter_file(ParamFile);

  if (ns_bases != NULL) {
    nonstandards = space(33);
    c=ns_bases;
    i=sym=0;
    if (*c=='-') {
      sym=1; c++;
    }
    while (*c!='\0') {
      if (*c!=',') {
        nonstandards[i++]=*c++;
        nonstandards[i++]=*c;
        if ((sym)&&(*c!=*(c-1))) {
          nonstandards[i++]=*c;
          nonstandards[i++]=*(c-1);
        }
      }
      c++;
    }
  }

  istty = isatty(fileno(stdout))&&isatty(fileno(stdin));

  /* print user help if we get input from tty */
  if(istty){
    if(!zuker)
      printf("Use '&' to connect 2 sequences that shall form a complex.\n");
    if(fold_constrained){
      print_tty_constraint(VRNA_CONSTRAINT_DOT | VRNA_CONSTRAINT_X | VRNA_CONSTRAINT_ANG_BRACK | VRNA_CONSTRAINT_RND_BRACK);
      print_tty_input_seq_str("Input sequence (upper or lower case) followed by structure constraint\n");
    }
    else print_tty_input_seq();
  }

  /* set options we wanna pass to vrna_read_fasta_record() */
  if(istty)             read_opt |= VRNA_INPUT_NOSKIP_BLANK_LINES;
  if(!fold_constrained) read_opt |= VRNA_INPUT_NO_REST;

  /*
  #############################################
  # main loop: continue until end of file
  #############################################
  */
  while(
    !((rec_type = vrna_read_fasta_record(&rec_id, &rec_sequence, &rec_rest, NULL, read_opt))
        & (VRNA_INPUT_ERROR | VRNA_INPUT_QUIT))){

    /*
    ########################################################
    # init everything according to the data we've read
    ########################################################
    */
    if(rec_id){
      /* if(!istty) printf("%s\n", rec_id); */
      (void) sscanf(rec_id, ">%" XSTR(FILENAME_ID_LENGTH) "s", fname);
    }
    else fname[0] = '\0';

    /* convert DNA alphabet to RNA if not explicitely switched off */
    if(!noconv) str_DNA2RNA(rec_sequence);
    /* store case-unmodified sequence */
    orig_sequence = strdup(rec_sequence);
    /* convert sequence to uppercase letters only */
    str_uppercase(rec_sequence);

    vrna_fold_compound *vc = vrna_get_fold_compound(rec_sequence, &md, VRNA_OPTION_MFE | (circ ? 0 : VRNA_OPTION_HYBRID) | ((n_back > 0) ? VRNA_OPTION_PF : 0));
    length    = vc->length;

    structure = (char *) space((unsigned) length+1);

    /* parse the rest of the current dataset to obtain a structure constraint */
    if(fold_constrained){
      cstruc = NULL;
      int cp = cut_point;
      unsigned int coptions = (rec_id) ? VRNA_CONSTRAINT_MULTILINE : 0;
      coptions |= VRNA_CONSTRAINT_DOT | VRNA_CONSTRAINT_X | VRNA_CONSTRAINT_ANG_BRACK | VRNA_CONSTRAINT_RND_BRACK;
      getConstraint(&cstruc, (const char **)rec_rest, coptions);
      cstruc = tokenize(cstruc);
      if(cut_point != cp) nrerror("cut point in sequence and structure constraint differs");
      cl = (cstruc) ? (int)strlen(cstruc) : 0;

      if(cl == 0)           warn_user("structure constraint is missing");
      else if(cl < length)  warn_user("structure constraint is shorter than sequence");
      else if(cl > length)  nrerror("structure constraint is too long");

      if(cstruc) strncpy(structure, cstruc, sizeof(char)*(cl+1));
    }

    if(istty){
      if (cut_point == -1)
        printf("length = %d\n", length);
      else
        printf("length1 = %d\nlength2 = %d\n", cut_point-1, length-cut_point+1);
    }

    /*
    ########################################################
    # begin actual computations
    ########################################################
    */

    if((logML != 0 || dangles==1 || dangles==3) && dos == 0)
      if(deltap<=0) deltap = delta/100. + 0.001;
    if (deltap>0)
      print_energy = deltap;

    /* stochastic backtracking */
    if(n_back>0){
      double mfe, kT;
      char *ss;
      if (fname[0] != '\0') printf(">%s\n", fname);

      ss = (char *) space(strlen(rec_sequence)+1);
      strncpy(ss, structure, length);
      mfe = vrna_fold(vc, ss);
      /* rescale Boltzmann factors according to predicted MFE */
      vrna_rescale_pf_params(vc, &mfe);
      /* ignore return value, we are not interested in the free energy */
      (void)vrna_pf_fold(vc, ss);

      free(ss);
      for (i=0; i<n_back; i++) {
        char *s;
        s = vrna_pbacktrack(vc);
        printf("%s\n", s);
        free(s);
      }
    }
    /* normal subopt */
    else if(!zuker){
      /* first lines of output (suitable  for sort +1n) */
      if (fname[0] != '\0')
        printf("> %s [%d]\n", fname, delta);

      vrna_subopt(vc, delta, stdout);

      if (dos) {
        int i;
        for (i=0; i<= MAXDOS && i<=delta/10; i++) {
          printf("%4d %6d\n", i, density_of_states[i]);
        }
      }
    }
    /* Zuker suboptimals */
    else{
      SOLUTION *zr;
      int i;
      if (fname[0] != '\0') printf(">%s\n%s\n", fname, rec_sequence);

      if (cut_point!=-1) {
        nrerror("Sorry, zuker subopts not yet implemented for cofold\n");
      }
      zr = vrna_zukersubopt(vc);

      putoutzuker(zr);
      (void)fflush(stdout);
      for (i=0; zr[i].structure; i++) {
        free(zr[i].structure);
      }
      free(zr);
    }
    
    (void)fflush(stdout);

    /* clean up */
    vrna_free_fold_compound(vc);

    if(cstruc) free(cstruc);
    if(rec_id) free(rec_id);
    free(rec_sequence);
    free(orig_sequence);
    free(structure);

    /* free the rest of current dataset */
    if(rec_rest){
      for(i=0;rec_rest[i];i++) free(rec_rest[i]);
      free(rec_rest);
    }
    rec_id = rec_sequence = orig_sequence = structure = cstruc = NULL;
    rec_rest = NULL;

    /* print user help for the next round if we get input from tty */
    if(istty){
      if(!zuker)
        printf("Use '&' to connect 2 sequences that shall form a complex.\n");
      if(fold_constrained){
        print_tty_constraint(VRNA_CONSTRAINT_DOT | VRNA_CONSTRAINT_X | VRNA_CONSTRAINT_ANG_BRACK | VRNA_CONSTRAINT_RND_BRACK);
        print_tty_input_seq_str("Input sequence (upper or lower case) followed by structure constraint\n");
      }
      else print_tty_input_seq();
    }
  }
  return EXIT_SUCCESS;
}

PRIVATE char *tokenize(char *line)
{
  char *pos, *copy;
  int cut = -1;

  copy = (char *) space(strlen(line)+1);
  (void) sscanf(line, "%s", copy);
  pos = strchr(copy, '&');
  if (pos) {
    cut = (int) (pos-copy)+1;
    if (cut >= strlen(copy)) cut = -1;
    if (strchr(pos+1, '&')) nrerror("more than one cut-point in input");
    for (;*pos;pos++) *pos = *(pos+1); /* splice out the & */
  }
  if (cut > -1) {
    if (cut_point==-1) cut_point = cut;
    else if (cut_point != cut) {
      fprintf(stderr,"cut_point = %d cut = %d\n", cut_point, cut);
      nrerror("Sequence and Structure have different cut points.");
    }
  }

  free(line);
  return copy;
}
PRIVATE void putoutzuker(SOLUTION* zukersolution) {
  int i;
  printf("%s [%.2f]\n",zukersolution[0].structure,zukersolution[0].energy/100.);
  for(i=1; zukersolution[i].structure; i++) {
    printf("%s [%.2f]\n", zukersolution[i].structure, zukersolution[i].energy/100.);
  }
  return;
}
