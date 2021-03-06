/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD (revised)
license as described in the file LICENSE.
 */
#include <fstream>
#include <float.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netdb.h>
#endif

#include "gd.h"
#include "rand48.h"
#include "reductions.h"

using namespace std;

using namespace LEARNER;

namespace GDMF {
  struct gdmf {
    vw* all;
    uint32_t rank;
  };

void mf_print_offset_features(gdmf& d, example& ec, size_t offset)
{
  vw& all = *d.all;
  weight* weights = all.reg.weight_vector;
  size_t mask = all.reg.weight_mask;
  for (unsigned char* i = ec.indices.begin; i != ec.indices.end; i++) 
    if (ec.audit_features[*i].begin != ec.audit_features[*i].end)
      for (audit_data *f = ec.audit_features[*i].begin; f != ec.audit_features[*i].end; f++)
	{
	  cout << '\t' << f->space << '^' << f->feature << ':' << f->weight_index <<"(" << ((f->weight_index + offset) & mask)  << ")" << ':' << f->x;

	  cout << ':' << weights[(f->weight_index + offset) & mask];
	}
    else
      for (feature *f = ec.atomics[*i].begin; f != ec.atomics[*i].end; f++)
	{
	  size_t index = (f->weight_index + offset) & all.reg.weight_mask;
	  
	  cout << "\tConstant:";
	  cout << ((index >> all.reg.stride_shift) & all.parse_mask) << ':' << f->x;
	  cout  << ':' << weights[index];
	}
  for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
    if (ec.atomics[(int)(*i)[0]].size() > 0 && ec.atomics[(int)(*i)[1]].size() > 0)
      {
	/* print out nsk^feature:hash:value:weight:nsk^feature^:hash:value:weight:prod_weights */
	for (size_t k = 1; k <= d.rank; k++)
	  {
	    for (audit_data* f = ec.audit_features[(int)(*i)[0]].begin; f!= ec.audit_features[(int)(*i)[0]].end; f++)
	      for (audit_data* f2 = ec.audit_features[(int)(*i)[1]].begin; f2!= ec.audit_features[(int)(*i)[1]].end; f2++)
		{
		  cout << '\t' << f->space << k << '^' << f->feature << ':' << ((f->weight_index+k)&mask) 
		       <<"(" << ((f->weight_index + offset +k) & mask)  << ")" << ':' << f->x;
		  cout << ':' << weights[(f->weight_index + offset + k) & mask];
		  
		  cout << ':' << f2->space << k << '^' << f2->feature << ':' << ((f2->weight_index+k+d.rank)&mask) 
		       <<"(" << ((f2->weight_index + offset +k+d.rank) & mask)  << ")" << ':' << f2->x;
		  cout << ':' << weights[(f2->weight_index + offset + k+d.rank) & mask];
		  
		  cout << ':' <<  weights[(f->weight_index + offset + k) & mask] * weights[(f2->weight_index + offset + k + d.rank) & mask];
		}
	  }
      }
  if (all.triples.begin() != all.triples.end()) {
    cerr << "cannot use triples in matrix factorization" << endl;
    throw exception();
  }
  cout << endl;
}

void mf_print_audit_features(gdmf& d, example& ec, size_t offset)
{
  print_result(d.all->stdout_fileno,ec.pred.scalar,-1,ec.tag);
  mf_print_offset_features(d, ec, offset);
}

float mf_predict(gdmf& d, example& ec)
{
  vw& all = *d.all;
  label_data& ld = ec.l.simple;
  float prediction = ld.initial;

  for (vector<string>::iterator i = d.all->pairs.begin(); i != d.all->pairs.end();i++)
    {
      ec.num_features -= ec.atomics[(int)(*i)[0]].size() * ec.atomics[(int)(*i)[1]].size();
      ec.num_features += ec.atomics[(int)(*i)[0]].size() * d.rank;
      ec.num_features += ec.atomics[(int)(*i)[1]].size() * d.rank;
    }

  // clear stored predictions
  ec.topic_predictions.erase();

  float linear_prediction = 0.;
  // linear terms
  for (unsigned char* i = ec.indices.begin; i != ec.indices.end; i++) 
    GD::foreach_feature<float, GD::vec_add>(all.reg.weight_vector, all.reg.weight_mask, ec.atomics[*i].begin, ec.atomics[*i].end, linear_prediction);

  // store constant + linear prediction
  // note: constant is now automatically added
  ec.topic_predictions.push_back(linear_prediction);
  
  prediction += linear_prediction;

  // interaction terms
  for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
    {
      if (ec.atomics[(int)(*i)[0]].size() > 0 && ec.atomics[(int)(*i)[1]].size() > 0)
	{
	  for (uint32_t k = 1; k <= d.rank; k++)
	    {
	      // x_l * l^k
	      // l^k is from index+1 to index+d.rank
	      //float x_dot_l = sd_offset_add(weights, mask, ec.atomics[(int)(*i)[0]].begin, ec.atomics[(int)(*i)[0]].end, k);
              float x_dot_l = 0.;
	      GD::foreach_feature<float, GD::vec_add>(all.reg.weight_vector, all.reg.weight_mask, ec.atomics[(int)(*i)[0]].begin, ec.atomics[(int)(*i)[0]].end, x_dot_l, k);
	      // x_r * r^k
	      // r^k is from index+d.rank+1 to index+2*d.rank
	      //float x_dot_r = sd_offset_add(weights, mask, ec.atomics[(int)(*i)[1]].begin, ec.atomics[(int)(*i)[1]].end, k+d.rank);
              float x_dot_r = 0.;
	      GD::foreach_feature<float,GD::vec_add>(all.reg.weight_vector, all.reg.weight_mask, ec.atomics[(int)(*i)[1]].begin, ec.atomics[(int)(*i)[1]].end, x_dot_r, k+d.rank);

	      prediction += x_dot_l * x_dot_r;

	      // store prediction from interaction terms
	      ec.topic_predictions.push_back(x_dot_l);
	      ec.topic_predictions.push_back(x_dot_r);
	    }
	}
    }

  if (all.triples.begin() != all.triples.end()) {
    cerr << "cannot use triples in matrix factorization" << endl;
    throw exception();
  }

  // ec.topic_predictions has linear, x_dot_l_1, x_dot_r_1, x_dot_l_2, x_dot_r_2, ... 

  ec.partial_prediction = prediction;

  all.set_minmax(all.sd, ld.label);

  ec.pred.scalar = GD::finalize_prediction(all.sd, ec.partial_prediction);
  
  if (ld.label != FLT_MAX)
    ec.loss = all.loss->getLoss(all.sd, ec.pred.scalar, ld.label) * ld.weight;
  
  if (all.audit)
    mf_print_audit_features(d, ec, 0);
  
  return ec.pred.scalar;
}


void sd_offset_update(weight* weights, size_t mask, feature* begin, feature* end, size_t offset, float update, float regularization)
{
  for (feature* f = begin; f!= end; f++) 
    weights[(f->weight_index + offset) & mask] += update * f->x - regularization * weights[(f->weight_index + offset) & mask];
}

  void mf_train(gdmf& d, example& ec)
  {
    vw& all = *d.all;
    weight* weights = all.reg.weight_vector;
    size_t mask = all.reg.weight_mask;
    label_data& ld = ec.l.simple;
    
    // use final prediction to get update size
      // update = eta_t*(y-y_hat) where eta_t = eta/(3*t^p) * importance weight
    float eta_t = all.eta/pow(ec.example_t,all.power_t) / 3.f * ld.weight;
    float update = all.loss->getUpdate(ec.pred.scalar, ld.label, eta_t, 1.); //ec.total_sum_feat_sq);
    
    float regularization = eta_t * all.l2_lambda;
    
    // linear update
    for (unsigned char* i = ec.indices.begin; i != ec.indices.end; i++) 
      sd_offset_update(weights, mask, ec.atomics[*i].begin, ec.atomics[*i].end, 0, update, regularization);
    
    // quadratic update
    for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
      {
	if (ec.atomics[(int)(*i)[0]].size() > 0 && ec.atomics[(int)(*i)[1]].size() > 0)
	  {
	    
	    // update l^k weights
	    for (size_t k = 1; k <= d.rank; k++)
	      {
		// r^k \cdot x_r
		float r_dot_x = ec.topic_predictions[2*k];
		// l^k <- l^k + update * (r^k \cdot x_r) * x_l
		sd_offset_update(weights, mask, ec.atomics[(int)(*i)[0]].begin, ec.atomics[(int)(*i)[0]].end, k, update*r_dot_x, regularization);
	      }
	    // update r^k weights
	    for (size_t k = 1; k <= d.rank; k++)
	      {
		// l^k \cdot x_l
		float l_dot_x = ec.topic_predictions[2*k-1];
		// r^k <- r^k + update * (l^k \cdot x_l) * x_r
		sd_offset_update(weights, mask, ec.atomics[(int)(*i)[1]].begin, ec.atomics[(int)(*i)[1]].end, k+d.rank, update*l_dot_x, regularization);
	      }
	    
	  }
      }
    if (all.triples.begin() != all.triples.end()) {
      cerr << "cannot use triples in matrix factorization" << endl;
      throw exception();
    }
  }  
  
  void save_load(gdmf& d, io_buf& model_file, bool read, bool text)
{
  vw* all = d.all;
  uint32_t length = 1 << all->num_bits;
  uint32_t stride_shift = all->reg.stride_shift;

  if(read)
    {
      initialize_regressor(*all);
      if(all->random_weights)
	for (size_t j = 0; j < (length << stride_shift); j++)
	  all->reg.weight_vector[j] = (float) (0.1 * frand48()); 
    }

  if (model_file.files.size() > 0)
    {
      uint32_t i = 0;
      uint32_t text_len;
      char buff[512];
      size_t brw = 1;

      do 
	{
	  brw = 0;
	  size_t K = d.rank*2+1;
	  
	  text_len = sprintf(buff, "%d ", i);
	  brw += bin_text_read_write_fixed(model_file,(char *)&i, sizeof (i),
					   "", read,
					   buff, text_len, text);
	  if (brw != 0)
	    for (uint32_t k = 0; k < K; k++)
	      {
		uint32_t ndx = (i << stride_shift)+k;
		
		weight* v = &(all->reg.weight_vector[ndx]);
		text_len = sprintf(buff, "%f ", *v);
		brw += bin_text_read_write_fixed(model_file,(char *)v, sizeof (*v),
						 "", read,
						 buff, text_len, text);
		
	      }
	  if (text)
	    brw += bin_text_read_write_fixed(model_file,buff,0,
					     "", read,
					     "\n",1,text);
	  
	  if (!read)
	    i++;
	}  
      while ((!read && i < length) || (read && brw >0));
    }
}
  
  void end_pass(gdmf& d)
  {
    vw* all = d.all;
    
    all->eta *= all->eta_decay_rate;
    if (all->save_per_pass)
      save_predictor(*all, all->final_regressor_name, all->current_pass);
    
    all->current_pass++;
  }

  void predict(gdmf& d, base_learner&, example& ec) { mf_predict(d,ec); }

  void learn(gdmf& d, base_learner& base, example& ec)
  {
    vw& all = *d.all;
 
    mf_predict(d, ec);
    if (all.training && ec.l.simple.label != FLT_MAX)
      mf_train(d, ec);
  }

  base_learner* setup(vw& all)
  {
    new_options(all, "Gdmf options")
      ("rank", po::value<uint32_t>(), "rank for matrix factorization.");
    if(no_new_options(all)) return NULL;

    gdmf& data = calloc_or_die<gdmf>(); 
    data.all = &all;
    data.rank = all.vm["rank"].as<uint32_t>();

    *all.file_options << " --rank " << data.rank;
    // store linear + 2*rank weights per index, round up to power of two
    float temp = ceilf(logf((float)(data.rank*2+1)) / logf (2.f));
    all.reg.stride_shift = (size_t) temp;
    all.random_weights = true;
    
    if ( all.vm.count("adaptive") )
      {
	cerr << "adaptive is not implemented for matrix factorization" << endl;
	throw exception();
      }
    if ( all.vm.count("normalized") )
      {
	cerr << "normalized is not implemented for matrix factorization" << endl;
	throw exception();
      }
    if ( all.vm.count("exact_adaptive_norm") )
      {
	cerr << "normalized adaptive updates is not implemented for matrix factorization" << endl;
	throw exception();
      }
    if (all.vm.count("bfgs") || all.vm.count("conjugate_gradient"))
      {
	cerr << "bfgs is not implemented for matrix factorization" << endl;
	throw exception();
      }	
    
    if(!all.vm.count("learning_rate") && !all.vm.count("l"))
      all.eta = 10; //default learning rate to 10 for non default update rule
    
    //default initial_t to 1 instead of 0
    if(!all.vm.count("initial_t")) {
      all.sd->t = 1.f;
      all.sd->weighted_unlabeled_examples = 1.f;
      all.initial_t = 1.f;
    }
    all.eta *= powf((float)(all.sd->t), all.power_t);

    learner<gdmf>& l = init_learner(&data, learn, 1 << all.reg.stride_shift);
    l.set_predict(predict);
    l.set_save_load(save_load);
    l.set_end_pass(end_pass);

    return make_base(l);
  }
}
