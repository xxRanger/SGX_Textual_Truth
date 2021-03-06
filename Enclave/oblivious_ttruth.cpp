//
// Created by anxin on 2020/4/11.
//

#include "oblivious_ttruth.h"

vector<int> oblivious_get_solution_provide_indicator(vector<Observation> &observations) {
	int user_num = observations.size();
	vector<int> sol_provide_ind(user_num);
	for (int j = 0; j < user_num; j++) {
		int total_observations = std::accumulate(observations[j].begin(),
				observations[j].end(), 0);
		sol_provide_ind[j] = total_observations > 0;
	}
	return sol_provide_ind;
}

void oblivious_sphere_kmeans(vector<Keyword> &keywords, WordModel &word_model,
		int cluster_num, int max_iter, double tol) {
	int dimension = 0;
	if (keywords.size() != 0)
		dimension = word_model.dimension;
	// init with kmeans ++
	vector<WordVec> clusters = oblivious_kmeans_init(keywords, word_model,
			cluster_num);

	vector<const WordVec*> keyword_vec;
	keyword_vec.reserve(keywords.size());

	for (int i = 0; i < keywords.size(); i++) {
		auto vec = &word_model.get_vec(keywords[i].content);
		//ocall_print_float_array(&vec[0],vec.size());
		if(vec->size()!=word_model.dimension) {
			throw "wrong dimension";
		}
		keyword_vec.push_back(vec);
	}

	for (int t = 0; t < max_iter; t++) {
		// e step
		// assign new cluster
		for (int i = 0; i < keywords.size(); i++) {
			auto &keyword = keywords[i];
			float max_prob = INT_MIN;
			int max_index = -1;
			for (int k = 0; k < clusters.size(); k++) {
				float prob = hpc::dot_product(clusters[k], *keyword_vec[i]);
				if (prob > max_prob) {
					max_prob = prob;
					max_index = k;
				}
			}
			keyword.cluster_assignment = max_index;
		}
		// M step
		// update parameters
		vector<WordVec> new_clusters(cluster_num, WordVec(dimension, 0));
		for (int i = 0; i < keywords.size(); i++) {
			auto &keyword = keywords[i];
			int cluster_assignment = keyword.cluster_assignment;
			WordVec tmp(*keyword_vec[i]);
			hpc::vector_mul_inplace(tmp, keyword.owner_id != -1);
			hpc::vector_add_inplace(new_clusters[cluster_assignment], tmp);
		}

		//normalize mu
		for (int i = 0; i < cluster_num; i++) {
			auto &mu = new_clusters[i];
			float mu_l2 = sqrt(hpc::dot_product(mu, mu));
			if (mu_l2 < 1e-8)
				continue; //
			hpc::vector_mul_inplace(mu, 1.0 / mu_l2);
		}
		// check stop tol
		double cur_tol = 0;
		for (int i = 0; i < cluster_num; i++) {
			WordVec diff = hpc::vector_sub(new_clusters[i], clusters[i]);
			double square_norm = sqrt(hpc::dot_product(diff, diff));
			cur_tol += square_norm;
		}

		if (cur_tol < tol) {
//            cout << "sphere clustering converge at iteration " << t << endl;
			break;
		}

		//set new cluster
		for (int i = 0; i < cluster_num; i++) {
			clusters[i] = std::move(new_clusters[i]);
		}
	}

//     see how many words each cluster have
	vector<int> cluster_histogram(cluster_num,0);
	    string countinfo;
	    for(int k=0;k<keywords.size();k++) {
	        cluster_histogram[keywords[k].cluster_assignment]+=1;

	    }

	    /*for(int k=0;k<cluster_histogram.size();k++) {
	    	countinfo+=to_string(cluster_histogram[k])+" ";
	    }
	    countinfo+="\n";
	    ocall_print_string(countinfo.c_str());*/
}

inline float distance_metrics(const WordVec &a, const WordVec &b) {
	return abs(2 - 2 * hpc::dot_product(a, b));
}


vector<WordVec> oblivious_kmeans_init(vector<Keyword> &keywords,
		WordModel &word_model, int cluster_num) {

	vector<const WordVec*> keyword_vec;
	keyword_vec.reserve(keywords.size());

	for (int i = 0; i < keywords.size(); i++) {
		auto vec = &word_model.get_vec(keywords[i].content);
		//ocall_print_float_array(&vec[0],vec.size());
		if(vec->size()!=word_model.dimension) {
			throw "wrong dimension";
		}
		keyword_vec.push_back(vec);
	}

	vector<WordVec> clusters;
	int random_num = (double) myRand::myrand_int()/myRand::MY_RAND_MAX*keywords.size();

	int first_index = random_num % keywords.size();
	clusters.push_back(*keyword_vec[first_index]);
	vector<float> min_dis_cache(keywords.size(), INT_MAX);
	vector<float> cluster_distance(keywords.size());
//    vector<int> clusters_index(cluster_num);
//    clusters_index[0] = first_index;
	for (int i = 1; i < cluster_num; i++) {
		float total_dis = 0;
		for (int j = 0; j < keywords.size(); j++) {
			auto &keyword = keywords[j];
			float min_dis = min_dis_cache[j];
			float dis = distance_metrics(*keyword_vec[j], clusters[i - 1]);
			min_dis = min(min_dis, dis);
			min_dis_cache[j] = min_dis;
			min_dis *= (keyword.owner_id != -1);
			cluster_distance[j] = min_dis;
			total_dis += min_dis;
		}
		// normalize distance
		for (auto &cd : cluster_distance) {
			cd /= total_dis;
		}
		// sample from distribution
		for (int j = 1; j < cluster_distance.size(); j++) {
			cluster_distance[j] += cluster_distance[j - 1];
		}
		cluster_distance[cluster_distance.size() - 1] = 1;

		double prob = myRand::myrandom();
		int cluster_index = -1;
		cluster_index = oblivious_assign_CMOV(prob <= cluster_distance[0], 0,
				cluster_index);
		bool larger = false;
		bool first_occur = true;
		for (int j = 1; j < cluster_distance.size(); j++) {
			larger |= prob > cluster_distance[j - 1];
			bool flag = larger && prob < cluster_distance[j]
					&& keywords[j].owner_id != -1 && first_occur;
			first_occur = oblivious_assign_CMOV(flag, false, first_occur);
			cluster_index = oblivious_assign_CMOV(flag, j, cluster_index);
		}
//        clusters_index[i] = cluster_index;
		clusters.push_back(*keyword_vec[cluster_index]);
	}
	return clusters;
}

void oblivious_observation_update(vector<Observation> &observations,
		vector<Keyword> &keywords) {
	for (auto &keyword : keywords) {
		int owner_id = keyword.owner_id;
		int cluster_assignment = keyword.cluster_assignment;
		int cluster_num = observations[owner_id].size();
		for (int i = 0; i < cluster_num; i++) {
			observations[owner_id][i] |= 1 * (cluster_assignment == i);
		}
	}
}

vector<vector<int>> oblivious_latent_truth_model(
		vector<vector<Observation>> &question_observations, int max_iter) {
	//random initialize truth indicator of each question and user prior count
	int question_num = question_observations.size();
	if (question_num == 0)
		throw "question size should be larger than 0";
	int user_num = question_observations[0].size();
	if (user_num == 0)
		throw "user size be larger than 0";
	int key_factor_num = question_observations[0][0].size();
	vector<vector<int>> question_indicator(question_num,
			vector<int>(key_factor_num, 0));
	vector<PriorCount> user_prior_count(user_num, PriorCount(4));
	for (int question_id = 0; question_id < question_num; question_id++) {
		auto &observations = question_observations[question_id];
		// an space efficient way to decide if user has provide solutions for the question
		vector<int> sol_provide_ind = oblivious_get_solution_provide_indicator(
				observations);
		// update prior count of each user
		for (int j = 0; j < key_factor_num; j++) {

			int indicator = myRand::myrand_int() % 2;
			question_indicator[question_id][j] = indicator;
			for (int k = 0; k < user_num; k++) {
				// if user does not provide solution for the question
				if (sol_provide_ind[k] == 0)
					continue;
				int ob = observations[k][j];
				for (int q = 0; q < 4; q++) {
					user_prior_count[k][q] += 1 * (indicator * 2 + ob == q);
				}

			}
		}
	}

	// gibbs sampling to infer truth
	for (int t = 0; t < max_iter; t++) {
		// update truth indicator
		for (int question_id = 0; question_id < question_num; question_id++) {
			auto &observations = question_observations[question_id];
			auto &truth_indicator = question_indicator[question_id];
			vector<int> sol_provide_ind = oblivious_get_solution_provide_indicator(
					observations);
			for (int j = 0; j < key_factor_num; j++) {
				double p[2] = { log(Question::BETA[0]), log(Question::BETA[1]) };
				int old_indicator = truth_indicator[j];
				for (int indicator = 0; indicator <= 1; ++indicator) {

					for (int k = 0; k < user_num; k++) {
						if (sol_provide_ind[k] == 0)
							continue; //user doest not offer observation
						int observation = observations[k][j];

						int alpha_observe0 = User::get_alpha(indicator, 0);
						int alpha_observe1 = User::get_alpha(indicator, 1);

						int prior_count_observe0 = user_prior_count[k][indicator
								* 2];
						int prior_count_observe1 = user_prior_count[k][indicator
								* 2 + 1];

						int alpha_observe = oblivious_assign_CMOV(observation,
								alpha_observe1, alpha_observe0);

						int prior_count_observe = oblivious_assign_CMOV(
								observation, prior_count_observe1,
								prior_count_observe0);

						int tmp = 1 * (old_indicator == indicator);
						prior_count_observe -= tmp;
						p[indicator] += log(
								(double) (prior_count_observe + alpha_observe)
										/ (alpha_observe0 + alpha_observe1
												+ prior_count_observe0
												+ prior_count_observe1));
					}
				}
				// assign 1 or 0 to truth indicator according to p
				//                truth_indicator[j] = p[0] > p[1] ? 0:1;
				//                 normalize probability, adjust the larger one to log 1
				double diff = 0;
				const int scale_factor = 1e8;
				int diff_tmp = oblivious_assign_CMOV(p[0] > p[1],
						p[0] * scale_factor, p[1] * scale_factor);
				diff = -(double) diff_tmp / scale_factor;
				p[0] += diff;
				p[1] += diff;
				p[0] = exp(p[0]);
				p[1] = exp(p[1]);

				double threshold = myRand::myrandom();
				int new_indicator = oblivious_assign_CMOV(
						threshold < p[0] / (p[0] + p[1]), 0, 1);

				truth_indicator[j] = new_indicator;
				for (int k = 0; k < user_num; k++) {
					if (sol_provide_ind[k] == 0)
						continue;
					int observation = observations[k][j];
					int add_index = new_indicator * 2 + observation;
					int decline_index = old_indicator * 2 + observation;
					for (int q = 0; q < 4; ++q) {
						user_prior_count[k][q] += 1 * (q == add_index);
						user_prior_count[k][q] -= 1 * (q == decline_index);
					}

				}

			}
		}
	}
	/*for(int i=0;i<user_num;i++) {
	 ocall_print_int_array(&user_prior_count[i][0], 4);
	 }*/
	return question_indicator;
}
